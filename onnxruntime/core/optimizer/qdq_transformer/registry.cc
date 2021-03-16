// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "registry.h"

namespace onnxruntime {
DECLARE_QDQ_CREATOR(Conv, QDQConvTransformer)
DECLARE_QDQ_CREATOR(MaxPool, QDQSimpleTransformer)
DECLARE_QDQ_CREATOR(Reshape, QDQSimpleTransformer)
DECLARE_QDQ_CREATOR(Add, QDQAddTransformer)
DECLARE_QDQ_CREATOR(MaMul, QDQMatMulTransformer)
std::unordered_map<std::string, QDQRegistry::QDQTransformerCreator> QDQRegistry::qdqtransformer_creators_{
    REGISER_QDQ_CREATOR(Conv, QDQConvTransformer),
    REGISER_QDQ_CREATOR(MaxPool, QDQSimpleTransformer),
    REGISER_QDQ_CREATOR(Reshape, QDQSimpleTransformer),
    REGISER_QDQ_CREATOR(Add, QDQAddTransformer),
    REGISER_QDQ_CREATOR(MaMul, QDQMatMulTransformer),
};
}  // namespace onnxruntime
