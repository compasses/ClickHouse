#pragma once

#include <Core/Block.h>
#include <Core/Block_fwd.h>
#include <Interpreters/Aggregator.h>
#include <Processors/QueryPlan/ITransformingStep.h>
#include <QueryPipeline/SizeLimits.h>
#include <Storages/SelectQueryInfo.h>
#include <Interpreters/Context_fwd.h>

namespace DB
{

Block appendGroupingSetColumn(Block header);
Block generateOutputHeader(const Block & input_header, const Names & keys, bool use_nulls);

/// Aggregation. See AggregatingTransform.
class CudaAggregatingStep : public ITransformingStep
{
public:
    CudaAggregatingStep(
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
        ContextPtr context_);

    static Block appendGroupingColumn(const Block & block, const Names & keys, bool has_grouping, bool use_nulls);

    String getName() const override { return "CudaAggregating"; }

    void transformPipeline(QueryPipelineBuilder & pipeline, const BuildQueryPipelineSettings &) override;

    void describeActions(JSONBuilder::JSONMap & map) const override;

    void describeActions(FormatSettings &) const override;
    void describePipeline(FormatSettings & settings) const override;

    const Aggregator::Params & getParams() const { return params; }

    const auto & getGroupingSetsParamsList() const { return grouping_sets_params; }
    bool isGroupByUseNulls() const { return group_by_use_nulls; }

    bool inOrder() const { return !sort_description_for_merging.empty(); }
    bool explicitSortingRequired() const { return explicit_sorting_required_for_aggregation_in_order; }
    bool isGroupingSets() const { return !grouping_sets_params.empty(); }
    void applyOrder(SortDescription sort_description_for_merging_, SortDescription group_by_sort_description_);
    bool memoryBoundMergingWillBeUsed() const;
    void skipMerging() { skip_merging = true; }

    const SortDescription & getSortDescription() const override;

    bool canUseProjection() const;
    void requestOnlyMergeForAggregateProjection(const SharedHeader & input_header);

    void serializeSettings(QueryPlanSerializationSettings & settings) const override;
    void serialize(Serialization & ctx) const override;
    bool isSerializable() const override
    {
        return sort_description_for_merging.empty() && !explicit_sorting_required_for_aggregation_in_order;
    }

    QueryPlanStepPtr clone() const override;

    void enableMemoryBoundMerging() { memory_bound_merging_of_aggregation_results_enabled = true; }

    bool hasCorrelatedExpressions() const override { return false; }

    Aggregator::Params getAggregatorParameters() const { return params; }
    bool getFinal() const noexcept { return final; }
    void setFinal(bool new_value);
    size_t getMaxBlockSize() const noexcept { return max_block_size; }
    size_t getMaxBlockSizeForAggregationInOrder() const noexcept { return aggregation_in_order_max_block_bytes; }
    size_t getMergeThreads() const noexcept { return merge_threads; }
    size_t getTemporaryDataMergeThreads() const noexcept { return temporary_data_merge_threads; }
    bool shouldProduceResultsInBucketOrder() const noexcept { return should_produce_results_in_order_of_bucket_number; }
    bool usingMemoryBoundMerging() const noexcept { return memory_bound_merging_of_aggregation_results_enabled; }

    bool supportsDataflowStatisticsCollection() const override
    {
        return sort_description_for_merging.empty() && grouping_sets_params.empty();
    }

private:
    void updateOutputHeader() override;

    ContextPtr context;
    Aggregator::Params params;
    GroupingSetsParamsList grouping_sets_params;
    bool final;
    size_t max_block_size;
    size_t aggregation_in_order_max_block_bytes;
    size_t merge_threads;
    size_t temporary_data_merge_threads;
    bool skip_merging = false;

    bool group_by_use_nulls;

    SortDescription sort_description_for_merging;
    SortDescription group_by_sort_description;

    const bool should_produce_results_in_order_of_bucket_number;
    bool memory_bound_merging_of_aggregation_results_enabled;
    bool explicit_sorting_required_for_aggregation_in_order;

    Processors aggregating_in_order;
    Processors aggregating_sorted;
    Processors finalizing;

    Processors aggregating;
};

}
