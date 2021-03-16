﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "orttraining/core/agent/training_agent.h"
#include "core/session/IOBinding.h"
#include "orttraining/training_ops/cpu/controlflow/ort_tasks.h"

namespace onnxruntime {
namespace training {

TrainingAgent::TrainingAgent(InferenceSession *session) : inference_session_(session) {}

TrainingAgent::~TrainingAgent() {
};

common::Status TrainingAgent::RunForward(const RunOptions& run_options, onnxruntime::IOBinding& io_binding, int64_t& run_id) {
  return inference_session_->PartialRun(run_options, io_binding, run_id);
}

common::Status TrainingAgent::RunBackward(const RunOptions& run_options, onnxruntime::IOBinding& io_binding, int64_t run_id) {
  LOGS(*inference_session_->GetLogger(), VERBOSE) << "Running TrainingAgent::Backward() with run_id " << run_id;
  return inference_session_->PartialRun(run_options, io_binding, run_id);
}
}  // namespace training
}  // namespace onnxruntime
