// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/src/exprs/agg/count.h

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include "column/nullable_column.h"
#include "exprs/agg/aggregate.h"
#include "gutil/casts.h"

namespace starrocks::vectorized {

struct AggregateFunctionCountData {
    int64_t count = 0;
};

// count not null column
class CountAggregateFunction final
        : public AggregateFunctionBatchHelper<AggregateFunctionCountData, CountAggregateFunction> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).count = 0;
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const override {
        ++this->data(state).count;
    }

    void update_batch_single_state(FunctionContext* ctx, size_t batch_size, const Column** columns,
                                   AggDataPtr state) const override {
        this->data(state).count += batch_size;
    }

    void update_batch_single_state(FunctionContext* ctx, AggDataPtr state, const Column** columns,
                                   int64_t peer_group_start, int64_t peer_group_end, int64_t frame_start,
                                   int64_t frame_end) const override {
        this->data(state).count += (frame_end - frame_start);
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr state, size_t row_num) const override {
        DCHECK(column->is_numeric());
        const auto* input_column = down_cast<const Int64Column*>(column);
        data(state).count += input_column->get_data()[row_num];
    }

    void get_values(FunctionContext* ctx, ConstAggDataPtr state, Column* dst, size_t start, size_t end) const {
        DCHECK_GT(end, start);
        Int64Column* column = down_cast<Int64Column*>(dst);
        for (size_t i = start; i < end; ++i) {
            column->get_data()[i] = this->data(state).count;
        }
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr state, Column* to) const override {
        DCHECK(to->is_numeric());
        down_cast<Int64Column*>(to)->append(this->data(state).count);
    }

    void batch_serialize(size_t batch_size, const Buffer<AggDataPtr>& agg_states, size_t state_offset,
                         Column* to) const override {
        Int64Column* column = down_cast<Int64Column*>(to);
        Buffer<int64_t>& result_data = column->get_data();
        for (size_t i = 0; i < batch_size; i++) {
            result_data.emplace_back(data(agg_states[i] + state_offset).count);
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr state, Column* to) const override {
        DCHECK(to->is_numeric());
        down_cast<Int64Column*>(to)->append(this->data(state).count);
    }

    void batch_finalize(size_t batch_size, const Buffer<AggDataPtr>& agg_states, size_t state_offsets,
                        Column* to) const {
        batch_serialize(batch_size, agg_states, state_offsets, to);
    }

    void convert_to_serialize_format(const Columns& src, size_t chunk_size, ColumnPtr* dst) const override {
        Int64Column* column = down_cast<Int64Column*>((*dst).get());
        column->get_data().assign(chunk_size, 1);
    }

    std::string get_name() const override { return "count"; }
};

// count null_able column
class CountNullableAggregateFunction final
        : public AggregateFunctionBatchHelper<AggregateFunctionCountData, CountNullableAggregateFunction> {
public:
    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr state) const override {
        this->data(state).count = 0;
    }

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr state, size_t row_num) const override {
        this->data(state).count += !columns[0]->is_null(row_num);
    }

    void update_batch_single_state(FunctionContext* ctx, size_t batch_size, const Column** columns,
                                   AggDataPtr state) const override {
        if (columns[0]->is_nullable()) {
            const auto* nullable_column = down_cast<const NullableColumn*>(columns[0]);
            if (nullable_column->has_null()) {
                const uint8_t* null_data = nullable_column->immutable_null_column_data().data();
                for (size_t i = 0; i < batch_size; ++i) {
                    this->data(state).count += !null_data[i];
                }
            } else {
                this->data(state).count += nullable_column->size();
            }
        } else {
            this->data(state).count += batch_size;
        }
    }

    void update_batch_single_state(FunctionContext* ctx, AggDataPtr state, const Column** columns,
                                   int64_t peer_group_start, int64_t peer_group_end, int64_t frame_start,
                                   int64_t frame_end) const override {
        if (columns[0]->is_nullable()) {
            const auto* nullable_column = down_cast<const NullableColumn*>(columns[0]);
            if (nullable_column->has_null()) {
                const uint8_t* null_data = nullable_column->immutable_null_column_data().data();
                for (size_t i = frame_start; i < frame_end; ++i) {
                    this->data(state).count += !null_data[i];
                }
            } else {
                this->data(state).count += (frame_end - frame_start);
            }
        } else {
            this->data(state).count += (frame_end - frame_start);
        }
    }

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr state, size_t row_num) const override {
        DCHECK(column->is_numeric());
        const auto* input_column = down_cast<const Int64Column*>(column);
        data(state).count += input_column->get_data()[row_num];
    }

    void get_values(FunctionContext* ctx, ConstAggDataPtr state, Column* dst, size_t start, size_t end) const {
        DCHECK_GT(end, start);
        Int64Column* column = down_cast<Int64Column*>(dst);
        for (size_t i = start; i < end; ++i) {
            column->get_data()[i] = this->data(state).count;
        }
    }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr state, Column* to) const override {
        DCHECK(to->is_numeric());
        down_cast<Int64Column*>(to)->append(this->data(state).count);
    }

    void batch_serialize(size_t batch_size, const Buffer<AggDataPtr>& agg_states, size_t state_offset,
                         Column* to) const override {
        Int64Column* column = down_cast<Int64Column*>(to);
        Buffer<int64_t>& result_data = column->get_data();
        for (size_t i = 0; i < batch_size; i++) {
            result_data.emplace_back(data(agg_states[i] + state_offset).count);
        }
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr state, Column* to) const override {
        DCHECK(to->is_numeric());
        down_cast<Int64Column*>(to)->append(this->data(state).count);
    }

    void batch_finalize(size_t batch_size, const Buffer<AggDataPtr>& agg_states, size_t state_offsets,
                        Column* to) const {
        batch_serialize(batch_size, agg_states, state_offsets, to);
    }

    void convert_to_serialize_format(const Columns& src, size_t chunk_size, ColumnPtr* dst) const override {
        Int64Column* column = down_cast<Int64Column*>((*dst).get());
        if (src[0]->is_nullable()) {
            const auto* nullable_column = down_cast<const NullableColumn*>(src[0].get());
            if (nullable_column->has_null()) {
                Buffer<int64_t>& dst_data = column->get_data();
                dst_data.resize(chunk_size);
                const uint8_t* null_data = nullable_column->immutable_null_column_data().data();
                for (size_t i = 0; i < chunk_size; ++i) {
                    dst_data[i] = !null_data[i];
                }
            } else {
                column->get_data().assign(chunk_size, 1);
            }
        } else {
            column->get_data().assign(chunk_size, 1);
        }
    }

    std::string get_name() const override { return "count_nullable"; }
};

} // namespace starrocks::vectorized
