// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/orc/tree/main/c++/include/orc/Vector.hh

/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ORC_VECTOR_HH
#define ORC_VECTOR_HH

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <list>
#include <memory>
#include <stdexcept>
#include <vector>

#include "Int128.hh"
#include "MemoryPool.hh"
#include "orc/orc-config.hh"

namespace orc {

/**
   * The base class for each of the column vectors. This class handles
   * the generic attributes such as number of elements, capacity, and
   * notNull vector.
   */
struct ColumnVectorBatch {
    ColumnVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~ColumnVectorBatch();

    // the number of slots available
    uint64_t capacity;
    // the number of current occupied slots
    uint64_t numElements;
    // an array of capacity length marking non-null values
    DataBuffer<char> notNull;
    // whether there are any null values
    bool hasNulls;
    // whether the vector batch is encoded
    bool isEncoded;

    // custom memory pool
    MemoryPool& memoryPool;

    /**
     * Generate a description of this vector as a string.
     */
    virtual std::string toString() const = 0;

    /**
     * Change the number of slots to at least the given capacity.
     * This function is not recursive into subtypes.
     */
    virtual void resize(uint64_t capacity);

    /**
     * Empties the vector from all its elements, recursively.
     * Do not alter the current capacity.
     */
    virtual void clear();

    /**
     * Heap memory used by the batch.
     */
    virtual uint64_t getMemoryUsage();

    /**
     * Check whether the batch length varies depending on data.
     */
    virtual bool hasVariableLength();

    // filter column vector batch
    // f_data: filter data
    // f_size: filter size
    // true_size: number of ones in filter data.
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size);

private:
    ColumnVectorBatch(const ColumnVectorBatch&);
    ColumnVectorBatch& operator=(const ColumnVectorBatch&);
};

struct LongVectorBatch : public ColumnVectorBatch {
    LongVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~LongVectorBatch();

    DataBuffer<int64_t> data;
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct DoubleVectorBatch : public ColumnVectorBatch {
    DoubleVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~DoubleVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();

    DataBuffer<double> data;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct StringVectorBatch : public ColumnVectorBatch {
    StringVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~StringVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();

    // pointers to the start of each string
    DataBuffer<char*> data;
    // the length of each string
    DataBuffer<int64_t> length;
    // string blob
    DataBuffer<char> blob;
    // dict codes, iff. there is dictionary.
    DataBuffer<int64_t> codes;
    bool use_codes;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct StringDictionary {
    StringDictionary(MemoryPool& pool);
    DataBuffer<char> dictionaryBlob;

    // Offset for each dictionary key entry.
    DataBuffer<int64_t> dictionaryOffset;

    void getValueByIndex(int64_t index, char*& valPtr, int64_t& length) {
        if (index < 0 || static_cast<uint64_t>(index) + 1 >= dictionaryOffset.size()) {
            throw std::out_of_range("index out of range.");
        }

        int64_t* offsetPtr = dictionaryOffset.data();

        valPtr = dictionaryBlob.data() + offsetPtr[index];
        length = offsetPtr[index + 1] - offsetPtr[index];
    }
};

/**
   * Include a index array with reference to corresponding dictionary.
   * User first obtain index from index array and retrieve string pointer
   * and length by calling getValueByIndex() from dictionary.
   */
struct EncodedStringVectorBatch : public StringVectorBatch {
    EncodedStringVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~EncodedStringVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    std::shared_ptr<StringDictionary> dictionary;

    // index for dictionary entry
    DataBuffer<int64_t> index;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct StructVectorBatch : public ColumnVectorBatch {
    StructVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~StructVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();
    bool hasVariableLength();

    std::vector<ColumnVectorBatch*> fields;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct ListVectorBatch : public ColumnVectorBatch {
    ListVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~ListVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();
    bool hasVariableLength();

    /**
     * The offset of the first element of each list.
     * The length of list i is offsets[i+1] - offsets[i].
     */
    DataBuffer<int64_t> offsets;

    // the concatenated elements
    ORC_UNIQUE_PTR<ColumnVectorBatch> elements;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct MapVectorBatch : public ColumnVectorBatch {
    MapVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~MapVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();
    bool hasVariableLength();

    /**
     * The offset of the first element of each map.
     * The size of map i is offsets[i+1] - offsets[i].
     */
    DataBuffer<int64_t> offsets;

    // the concatenated keys
    ORC_UNIQUE_PTR<ColumnVectorBatch> keys;
    // the concatenated elements
    ORC_UNIQUE_PTR<ColumnVectorBatch> elements;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct UnionVectorBatch : public ColumnVectorBatch {
    UnionVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~UnionVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();
    bool hasVariableLength();

    /**
     * For each value, which element of children has the value.
     */
    DataBuffer<unsigned char> tags;

    /**
     * For each value, the index inside of the child ColumnVectorBatch.
     */
    DataBuffer<uint64_t> offsets;

    // the sub-columns
    std::vector<ColumnVectorBatch*> children;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

struct Decimal {
    Decimal(const Int128& value, int32_t scale);
    explicit Decimal(const std::string& value);
    Decimal();

    std::string toString(bool trimTrailingZeros = false) const;
    Int128 value;
    int32_t scale;
};

struct Decimal64VectorBatch : public ColumnVectorBatch {
    Decimal64VectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~Decimal64VectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();

    // total number of digits
    int32_t precision;
    // the number of places after the decimal
    int32_t scale;

    // the numeric values
    DataBuffer<int64_t> values;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;

protected:
    /**
     * Contains the scales that were read from the file. Should NOT be
     * used.
     */
    DataBuffer<int64_t> readScales;
    friend class Decimal64ColumnReader;
    friend class Decimal64ColumnWriter;
};

struct Decimal128VectorBatch : public ColumnVectorBatch {
    Decimal128VectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~Decimal128VectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();

    // total number of digits
    int32_t precision;
    // the number of places after the decimal
    int32_t scale;

    // the numeric values
    DataBuffer<Int128> values;
    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;

protected:
    /**
     * Contains the scales that were read from the file. Should NOT be
     * used.
     */
    DataBuffer<int64_t> readScales;
    friend class Decimal128ColumnReader;
    friend class DecimalHive11ColumnReader;
    friend class Decimal128ColumnWriter;
};

/**
   * A column vector batch for storing timestamp values.
   * The timestamps are stored split into the time_t value (seconds since
   * 1 Jan 1970 00:00:00) and the nanoseconds within the time_t value.
   */
struct TimestampVectorBatch : public ColumnVectorBatch {
    TimestampVectorBatch(uint64_t capacity, MemoryPool& pool);
    virtual ~TimestampVectorBatch();
    std::string toString() const;
    void resize(uint64_t capacity);
    void clear();
    uint64_t getMemoryUsage();

    // the number of seconds past 1 Jan 1970 00:00 UTC (aka time_t)
    // Note that we always assume data is in GMT timezone; therefore it is
    // user's responsibility to convert wall clock time in local timezone
    // to GMT.
    DataBuffer<int64_t> data;

    // the nanoseconds of each value
    DataBuffer<int64_t> nanoseconds;

    virtual void filter(uint8_t* f_data, uint32_t f_size, uint32_t true_size) override;
};

} // namespace orc

#endif
