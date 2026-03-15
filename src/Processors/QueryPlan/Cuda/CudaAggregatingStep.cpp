#include <cassert>
#include <cstddef>
#include <memory>
#include <Columns/ColumnConst.h>
#include <Columns/ColumnFixedString.h>
#include <DataTypes/DataTypeFixedString.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypesNumber.h>
#include <Functions/FunctionFactory.h>
#include <IO/Operators.h>
#include <Interpreters/Aggregator.h>
#include <Interpreters/Context.h>
#include <Interpreters/ExpressionActions.h>
#include <Processors/Merges/AggregatingSortedTransform.h>
#include <Processors/Merges/FinishAggregatingInOrderTransform.h>
#include <Processors/QueryPlan/Cuda/CudaAggregatingStep.h>
#include <Processors/QueryPlan/QueryPlanSerializationSettings.h>
#include <Processors/QueryPlan/QueryPlanStepRegistry.h>
#include <Processors/QueryPlan/Serialization.h>
#include <Processors/QueryPlan/SortingStep.h>
#include <Processors/Transforms/AggregatingInOrderTransform.h>
#include <Processors/Transforms/Cuda/CudaAggregatingTransform.h>
#include <Processors/Transforms/CopyTransform.h>
#include <Processors/Transforms/ExpressionTransform.h>
#include <Processors/Transforms/MemoryBoundMerging.h>
#include <Processors/Transforms/MergingAggregatedMemoryEfficientTransform.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Common/JSONBuilder.h>

namespace DB
{

namespace QueryPlanSerializationSetting
{
    extern const QueryPlanSerializationSettingsUInt64 aggregation_in_order_max_block_bytes;
    extern const QueryPlanSerializationSettingsBool aggregation_in_order_memory_bound_merging;
    extern const QueryPlanSerializationSettingsBool aggregation_sort_result_by_bucket_number;
    extern const QueryPlanSerializationSettingsBool collect_hash_table_stats_during_aggregation;
    extern const QueryPlanSerializationSettingsBool compile_aggregate_expressions;
    extern const QueryPlanSerializationSettingsBool empty_result_for_aggregation_by_empty_set;
    extern const QueryPlanSerializationSettingsBool enable_software_prefetch_in_aggregation;
    extern const QueryPlanSerializationSettingsOverflowModeGroupBy group_by_overflow_mode;
    extern const QueryPlanSerializationSettingsUInt64 group_by_two_level_threshold_bytes;
    extern const QueryPlanSerializationSettingsUInt64 group_by_two_level_threshold;
    extern const QueryPlanSerializationSettingsUInt64 max_block_size;
    extern const QueryPlanSerializationSettingsUInt64 max_bytes_before_external_group_by;
    extern const QueryPlanSerializationSettingsUInt64 max_entries_for_hash_table_stats;
    extern const QueryPlanSerializationSettingsUInt64 max_rows_to_group_by;
    extern const QueryPlanSerializationSettingsUInt64 max_size_to_preallocate_for_aggregation;
    extern const QueryPlanSerializationSettingsUInt64 min_count_to_compile_aggregate_expression;
    extern const QueryPlanSerializationSettingsUInt64 min_free_disk_space_for_temporary_data;
    extern const QueryPlanSerializationSettingsFloat min_hit_rate_to_use_consecutive_keys_optimization;
    extern const QueryPlanSerializationSettingsBool optimize_group_by_constant_keys;
    extern const QueryPlanSerializationSettingsBool enable_producing_buckets_out_of_order_in_aggregation;
    extern const QueryPlanSerializationSettingsBool serialize_string_in_memory_with_zero_byte;
}

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
    extern const int NOT_IMPLEMENTED;
    extern const int INCORRECT_DATA;
}

static bool memoryBoundMergingWillBeUsed(
    bool should_produce_results_in_order_of_bucket_number,
    bool memory_bound_merging_of_aggregation_results_enabled,
    SortDescription sort_description_for_merging)
{
    return should_produce_results_in_order_of_bucket_number && memory_bound_merging_of_aggregation_results_enabled && !sort_description_for_merging.empty();
}

static ITransformingStep::Traits getTraits(bool should_produce_results_in_order_of_bucket_number)
{
    return ITransformingStep::Traits
    {
        {
            .returns_single_stream = should_produce_results_in_order_of_bucket_number,
            .preserves_number_of_streams = false,
            .preserves_sorting = false,
        },
        {
            .preserves_number_of_rows = false,
        }
    };
}

static inline void convertToNullable(Block & header, const Names & keys)
{
    for (const auto & key : keys)
    {
        auto & column = header.getByName(key);

        column.type = makeNullableSafe(column.type);
        column.column = makeNullableSafe(column.column);
    }
}

Block CudaAggregatingStep::appendGroupingColumn(const Block & block, const Names & keys, bool has_grouping, bool use_nulls)
{
    if (!has_grouping)
        return block;

    auto header = appendGroupingSetColumn(block);
    if (use_nulls)
        convertToNullable(header, keys);
    return header;
}

CudaAggregatingStep::CudaAggregatingStep(
    const SharedHeader & input_header_,
    Aggregator::Params params_,
    GroupingSetsParamsList grouping_sets_params_,
    bool final_,
    size_t max_block_size_,
    size_t aggregation_in_order_max_block_bytes_,
    size_t merge_threads_,
    size_t temporary_data_merge_threads_,
    bool group_by_use_nulls_,
    SortDescription sort_description_for_merging_,
    SortDescription group_by_sort_description_,
    bool should_produce_results_in_order_of_bucket_number_,
    bool memory_bound_merging_of_aggregation_results_enabled_,
    bool explicit_sorting_required_for_aggregation_in_order_,
    ContextPtr context_)
    : ITransformingStep(
        input_header_,
        std::make_shared<const Block>(appendGroupingColumn(params_.getHeader(*input_header_, final_), params_.keys, !grouping_sets_params_.empty(), group_by_use_nulls_)),
        getTraits(should_produce_results_in_order_of_bucket_number_),
        false)
    , context(context_)
    , params(std::move(params_))
    , grouping_sets_params(std::move(grouping_sets_params_))
    , final(final_)
    , max_block_size(max_block_size_)
    , aggregation_in_order_max_block_bytes(aggregation_in_order_max_block_bytes_)
    , merge_threads(merge_threads_)
    , temporary_data_merge_threads(temporary_data_merge_threads_)
    , group_by_use_nulls(group_by_use_nulls_)
    , sort_description_for_merging(std::move(sort_description_for_merging_))
    , group_by_sort_description(std::move(group_by_sort_description_))
    , should_produce_results_in_order_of_bucket_number(should_produce_results_in_order_of_bucket_number_)
    , memory_bound_merging_of_aggregation_results_enabled(memory_bound_merging_of_aggregation_results_enabled_)
    , explicit_sorting_required_for_aggregation_in_order(explicit_sorting_required_for_aggregation_in_order_)
{
}

void CudaAggregatingStep::applyOrder(SortDescription sort_description_for_merging_, SortDescription group_by_sort_description_)
{
    sort_description_for_merging = std::move(sort_description_for_merging_);
    group_by_sort_description = std::move(group_by_sort_description_);
    explicit_sorting_required_for_aggregation_in_order = false;
}

bool CudaAggregatingStep::memoryBoundMergingWillBeUsed() const
{
    return ::DB::memoryBoundMergingWillBeUsed(
        should_produce_results_in_order_of_bucket_number,
        memory_bound_merging_of_aggregation_results_enabled,
        sort_description_for_merging);
}

const SortDescription & CudaAggregatingStep::getSortDescription() const
{
    if (memoryBoundMergingWillBeUsed())
        return group_by_sort_description;

    return IQueryPlanStep::getSortDescription();
}

void CudaAggregatingStep::transformPipeline(QueryPipelineBuilder & pipeline, const BuildQueryPipelineSettings &)
{
    QueryPipelineProcessorsCollector collector(pipeline, this);

    pipeline.dropTotalsAndExtremes();

    bool allow_to_use_two_level_group_by = false;
    if (!allow_to_use_two_level_group_by)
    {
        params.group_by_two_level_threshold = 0;
        params.group_by_two_level_threshold_bytes = 0;
    }

    const auto src_header = pipeline.getHeader();
    auto transform_params = std::make_shared<CudaAggregatingTransformParams>(src_header, std::move(params), final, context);

    if (pipeline.getNumStreams() > 1)
    {
        auto many_data = std::make_shared<CudaAggregatedDataVariants>(pipeline.getNumStreams());

        size_t counter = 0;

        pipeline.addSimpleTransform(
            [&](const std::shared_ptr<const Block> & header)
            {
                return std::make_shared<CudaAggregatingTransform>(
                    *header, transform_params, many_data, counter++, merge_threads, context);
            });

        pipeline.resize(1);

        aggregating = collector.detachProcessors(0);
    }
    else
    {
        pipeline.resize(1);

        pipeline.addSimpleTransform([&](const std::shared_ptr<const Block> & header)
                                    { return std::make_shared<CudaAggregatingTransform>(*header, transform_params, context); });

        aggregating = collector.detachProcessors(0);
    }
}

void CudaAggregatingStep::describeActions(FormatSettings & settings) const
{
    params.explain(settings.out, settings.offset);
}

void CudaAggregatingStep::describeActions(JSONBuilder::JSONMap & map) const
{
    params.explain(map);
}

void CudaAggregatingStep::describePipeline(FormatSettings & settings) const
{
    if (!aggregating.empty())
        IQueryPlanStep::describePipeline(aggregating, settings);
    else
    {
        IQueryPlanStep::describePipeline(finalizing, settings);
        IQueryPlanStep::describePipeline(aggregating_sorted, settings);
        IQueryPlanStep::describePipeline(aggregating_in_order, settings);
    }
}

void CudaAggregatingStep::updateOutputHeader()
{
    output_header = std::make_shared<const Block>(
        appendGroupingColumn(params.getHeader(*input_headers.front(), final), params.keys, !grouping_sets_params.empty(), group_by_use_nulls));
}

bool CudaAggregatingStep::canUseProjection() const
{
    return false;
}

void CudaAggregatingStep::requestOnlyMergeForAggregateProjection(const SharedHeader &)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "CudaAggregatingStep does not support projections");
}

void CudaAggregatingStep::serializeSettings(QueryPlanSerializationSettings &) const
{
}

void CudaAggregatingStep::serialize(Serialization &) const
{
}

QueryPlanStepPtr CudaAggregatingStep::clone() const
{
    return std::make_unique<CudaAggregatingStep>(*this);
}

void CudaAggregatingStep::setFinal(bool)
{
    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "CudaAggregatingStep does not support setFinal");
}

}
