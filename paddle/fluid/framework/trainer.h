/* Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

#include <fstream>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "paddle/fluid/framework/data_feed.h"
#include "paddle/fluid/framework/data_set.h"
#include "paddle/fluid/framework/device_worker.h"
#include "paddle/fluid/framework/lod_tensor.h"
#include "paddle/fluid/framework/program_desc.h"
#include "paddle/fluid/framework/reader.h"
#include "paddle/fluid/framework/trainer_desc.pb.h"
#include "paddle/fluid/framework/variable_helper.h"
#include "paddle/fluid/operators/reader/blocking_queue.h"
#include "paddle/fluid/platform/port.h"

namespace paddle {
namespace framework {

class TrainerBase {
 public:
  TrainerBase() {}
  virtual ~TrainerBase() {}
  // model memory are hosted in root_scope
  void SetScope(Scope* root_scope);
  void SetDebug(const bool debug) { debug_ = debug; }
  void SetDataset(Dataset* dataset_ptr) { dataset_ptr_ = dataset_ptr; }
  virtual void Initialize(const TrainerDesc& trainer_desc,
                          Dataset* data_set) = 0;
  virtual void InitTrainerEnv(const ProgramDesc& main_program,
                              const platform::Place& place) = 0;
  virtual void InitOtherEnv(const ProgramDesc& main_program) = 0;
  virtual void Run() = 0;
  virtual void Finalize() = 0;
  virtual Scope* GetWorkerScope(int thread_id) = 0;
  virtual void InitDumpEnv() = 0;
  virtual void DumpWork(int tid);

 protected:
  virtual std::string GetDumpPath(int tid) = 0;
  virtual void ParseDumpConfig(const TrainerDesc& trainer_desc);
  virtual void FinalizeDumpEnv();

  Scope* root_scope_;
  bool debug_;
  Dataset* dataset_ptr_;

  // For dump param or field
  bool need_dump_field_ = false;
  bool need_dump_param_ = false;
  std::string dump_fields_path_;
  std::string dump_converter_;
  std::vector<std::string> dump_param_;
  std::vector<std::string> dump_fields_;
  int dump_thread_num_;
  std::vector<std::thread> dump_thread_;
  std::shared_ptr<paddle::framework::ChannelObject<std::string>> queue_;
};

// general trainer for async execution
// local trainer and distributed trainer are supported
// depends on the assigned device_worker
class MultiTrainer : public TrainerBase {
 public:
  MultiTrainer() {}
  virtual ~MultiTrainer() {}
  virtual void Initialize(const TrainerDesc& trainer_desc, Dataset* data_set);
  virtual void InitTrainerEnv(const ProgramDesc& main_program,
                              const platform::Place& place);
  virtual void InitOtherEnv(const ProgramDesc& main_program);
  virtual void Run();
  virtual void Finalize();
  virtual void InitDumpEnv();
  virtual Scope* GetWorkerScope(int thread_id);
  virtual std::string GetDumpPath(int tid);

 protected:
  int thread_num_;
  std::vector<std::thread> threads_;
  std::vector<DataFeed*> readers_;
  std::vector<std::shared_ptr<DeviceWorker>> workers_;
  std::vector<std::string> need_merge_var_names_;

  int mpi_rank_;
  int mpi_size_;
  int dump_file_num_;
};

class DistMultiTrainer : public MultiTrainer {
 public:
  DistMultiTrainer() {}
  virtual ~DistMultiTrainer() {}
  virtual void Initialize(const TrainerDesc& trainer_desc, Dataset* data_set);
  virtual void InitTrainerEnv(const ProgramDesc& main_program,
                              const platform::Place& place);
  virtual void InitOtherEnv(const ProgramDesc& main_program);
  virtual void Run();
  virtual void Finalize();
  template <typename T>
  void MergeToRootScope(LoDTensor* root_tensor, LoDTensor* thread_tensor);
  virtual void InitDumpEnv();
  virtual Scope* GetWorkerScope(int thread_id);

 protected:
  std::shared_ptr<paddle::framework::PullDenseWorker> pull_dense_worker_;
};

#if defined(PADDLE_WITH_NCCL)
class PipelineTrainer : public TrainerBase {
 public:
  PipelineTrainer() {}
  ~PipelineTrainer() override {}
  void Initialize(const TrainerDesc& trainer_desc, Dataset* data_set) override;
  void InitTrainerEnv(const ProgramDesc& main_program,
                      const platform::Place& place) override;
  void InitOtherEnv(const ProgramDesc& main_program) override;
  void Run() override;
  void Finalize() override;
  virtual Scope* GetWorkerScope(int thread_id);
  void InitDumpEnv() override;
  virtual std::string GetDumpPath(int tid);
  void GetSkipVars(int section_id, const ProgramDesc& main_program);

 protected:
  int section_num_;
  int num_microbatches_;
  int start_cpu_core_id_;
  std::vector<std::string> feed_var_names_;
  std::vector<platform::Place> places_;
  std::vector<std::vector<std::string>> skip_vars_;
  TrainerDesc trainer_desc_;

  std::vector<std::thread> section_threads_;
  // worker: [section_id]
  std::vector<std::shared_ptr<paddle::framework::DeviceWorker>> workers_;
  // minibatch_scopes_: [section_id]
  std::vector<Scope*> minibatch_scopes_;
  // microbatch_scopes_: [section_id][microbatch_id]
  std::vector<std::vector<Scope*>> microbatch_scopes_;

  void CopyParameters(int section_id, int microbatch_id,
                      const ProgramDesc& program, const platform::Place& place);
  bool isPersistableVarGrad(std::string name);
  bool isPersistable(VarDesc* var);
};
#endif

}  // namespace framework
}  // namespace paddle
