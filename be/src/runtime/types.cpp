// This file is made available under Elastic License 2.0.
// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/src/runtime/types.cpp

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

#include "runtime/types.h"

#include <ostream>

#include "gutil/strings/substitute.h"

namespace starrocks {

TypeDescriptor::TypeDescriptor(const std::vector<TTypeNode>& types, int* idx) : len(-1), precision(-1), scale(-1) {
    DCHECK_GE(*idx, 0);
    DCHECK_LT(*idx, types.size());
    const TTypeNode& node = types[*idx];
    switch (node.type) {
    case TTypeNodeType::SCALAR: {
        DCHECK(node.__isset.scalar_type);
        ++(*idx);
        const TScalarType scalar_type = node.scalar_type;
        type = thrift_to_type(scalar_type.type);
        len = (scalar_type.__isset.len) ? scalar_type.len : -1;
        scale = (scalar_type.__isset.scale) ? scalar_type.scale : -1;
        precision = (scalar_type.__isset.precision) ? scalar_type.precision : -1;

        if (type == TYPE_DECIMAL || type == TYPE_DECIMALV2 || type == TYPE_DECIMAL32 || type == TYPE_DECIMAL64 ||
            type == TYPE_DECIMAL128) {
            DCHECK(scalar_type.__isset.precision);
            DCHECK(scalar_type.__isset.scale);
        }
        break;
    }
    case TTypeNodeType::STRUCT:
        type = TYPE_STRUCT;
        ++(*idx);
        for (int i = 0; i < node.struct_fields.size(); ++i) {
            field_names.push_back(node.struct_fields[i].name);
            children.push_back(TypeDescriptor(types, idx));
        }
        break;
    case TTypeNodeType::ARRAY:
        DCHECK(!node.__isset.scalar_type);
        DCHECK_LT(*idx, types.size() - 1);
        ++(*idx);
        type = TYPE_ARRAY;
        children.push_back(TypeDescriptor(types, idx));
        break;
    case TTypeNodeType::MAP:
        DCHECK(!node.__isset.scalar_type);
        DCHECK_LT(*idx, types.size() - 2);
        type = TYPE_MAP;
        ++(*idx);
        children.push_back(TypeDescriptor(types, idx));
        children.push_back(TypeDescriptor(types, idx));
        break;
    }
}

void TypeDescriptor::to_thrift(TTypeDesc* thrift_type) const {
    thrift_type->__isset.types = true;
    thrift_type->types.emplace_back();
    TTypeNode& curr_node = thrift_type->types.back();
    if (type == TYPE_ARRAY) {
        curr_node.__set_type(TTypeNodeType::ARRAY);
        DCHECK_EQ(1, children.size());
        children[0].to_thrift(thrift_type);
    } else if (type == TYPE_MAP) {
        curr_node.__set_type(TTypeNodeType::MAP);
        DCHECK_EQ(2, children.size());
        children[0].to_thrift(thrift_type);
        children[1].to_thrift(thrift_type);
    } else if (type == TYPE_STRUCT) {
        DCHECK_EQ(type, TYPE_STRUCT);
        curr_node.__set_type(TTypeNodeType::STRUCT);
        curr_node.__set_struct_fields(std::vector<TStructField>());
        for (const auto& field_name : field_names) {
            curr_node.struct_fields.emplace_back();
            curr_node.struct_fields.back().__set_name(field_name);
        }
        for (const TypeDescriptor& child : children) {
            child.to_thrift(thrift_type);
        }
    } else {
        curr_node.type = TTypeNodeType::SCALAR;
        curr_node.__set_scalar_type(TScalarType());
        TScalarType& scalar_type = curr_node.scalar_type;
        scalar_type.__set_type(starrocks::to_thrift(type));
        if (len != -1) {
            scalar_type.__set_len(len);
        }
        if (scale != -1) {
            scalar_type.__set_scale(scale);
        }
        if (precision != -1) {
            scalar_type.__set_precision(precision);
        }
    }
}

void TypeDescriptor::to_protobuf(PTypeDesc* proto_type) const {
    PTypeNode* node = proto_type->add_types();
    if (type == TYPE_ARRAY) {
        node->set_type(TTypeNodeType::ARRAY);
        DCHECK_EQ(1, children.size());
        children[0].to_protobuf(proto_type);
    } else if (type == TYPE_MAP) {
        node->set_type(TTypeNodeType::MAP);
        DCHECK_EQ(2, children.size());
        children[0].to_protobuf(proto_type);
        children[1].to_protobuf(proto_type);
    } else if (type == TYPE_STRUCT) {
        node->set_type(TTypeNodeType::STRUCT);
        for (const auto& field_name : field_names) {
            node->add_struct_fields()->set_name(field_name);
        }
        for (const TypeDescriptor& child : children) {
            child.to_protobuf(proto_type);
        }
    } else {
        node->set_type(TTypeNodeType::SCALAR);
        PScalarType* scalar_type = node->mutable_scalar_type();
        scalar_type->set_type(starrocks::to_thrift(type));
        if (len != -1) {
            scalar_type->set_len(len);
        }
        if (scale != -1) {
            scalar_type->set_scale(scale);
        }
        if (precision != -1) {
            scalar_type->set_precision(precision);
        }
    }
}

TypeDescriptor::TypeDescriptor(const google::protobuf::RepeatedPtrField<PTypeNode>& types, int* idx)
        : len(-1), precision(-1), scale(-1) {
    DCHECK_GE(*idx, 0);
    DCHECK_LT(*idx, types.size());

    const PTypeNode& node = types.Get(*idx);
    TTypeNodeType::type node_type = static_cast<TTypeNodeType::type>(node.type());
    switch (node_type) {
    case TTypeNodeType::SCALAR: {
        DCHECK(node.has_scalar_type());
        ++(*idx);
        const PScalarType& scalar_type = node.scalar_type();
        type = thrift_to_type((TPrimitiveType::type)scalar_type.type());
        len = scalar_type.has_len() ? scalar_type.len() : -1;
        scale = scalar_type.has_scale() ? scalar_type.scale() : -1;
        precision = scalar_type.has_precision() ? scalar_type.precision() : -1;

        if (type == TYPE_CHAR || type == TYPE_VARCHAR || type == TYPE_HLL) {
            DCHECK(scalar_type.has_len());
        } else if (type == TYPE_DECIMAL || type == TYPE_DECIMALV2) {
            DCHECK(scalar_type.has_precision());
            DCHECK(scalar_type.has_scale());
        }
        break;
    }
    case TTypeNodeType::STRUCT:
        type = TYPE_STRUCT;
        ++(*idx);
        for (int i = 0; i < node.struct_fields().size(); ++i) {
            children.push_back(TypeDescriptor(types, idx));
            field_names.push_back(node.struct_fields(i).name());
        }
        break;
    case TTypeNodeType::ARRAY:
        DCHECK(!node.has_scalar_type());
        DCHECK_LT(*idx, types.size() - 1);
        ++(*idx);
        type = TYPE_ARRAY;
        children.push_back(TypeDescriptor(types, idx));
        break;
    case TTypeNodeType::MAP:
        DCHECK(!node.has_scalar_type());
        DCHECK_LT(*idx, types.size() - 2);
        ++(*idx);
        type = TYPE_MAP;
        children.push_back(TypeDescriptor(types, idx));
        children.push_back(TypeDescriptor(types, idx));
        break;
    }
}

std::string TypeDescriptor::debug_string() const {
    switch (type) {
    case TYPE_CHAR:
        return strings::Substitute("CHAR($0)", len);
    case TYPE_VARCHAR:
        return strings::Substitute("VARCHAR($0)", len);
    case TYPE_DECIMAL:
        return strings::Substitute("DECIMAL($0, $1)", precision, scale);
    case TYPE_DECIMALV2:
        return strings::Substitute("DECIMALV2($0, $1)", precision, scale);
    case TYPE_DECIMAL32:
        return strings::Substitute("DECIMAL32($0, $1)", precision, scale);
    case TYPE_DECIMAL64:
        return strings::Substitute("DECIMAL64($0, $1)", precision, scale);
    case TYPE_DECIMAL128:
        return strings::Substitute("DECIMAL128($0, $1)", precision, scale);
    case TYPE_ARRAY:
        return strings::Substitute("ARRAY<$0>", children[0].debug_string());
    case TYPE_MAP:
        return strings::Substitute("MAP<$0, $1>", children[0].debug_string(), children[1].debug_string());
    case TYPE_STRUCT: {
        std::stringstream ss;
        ss << "STRUCT{";
        for (size_t i = 0; i < field_names.size(); i++) {
            ss << field_names[i] << " " << children[i].debug_string();
            if (i + 1 < field_names.size()) ss << ", ";
        }
        ss << "}";
        return ss.str();
    }
    default:
        return type_to_string(type);
    }
}

} // namespace starrocks
