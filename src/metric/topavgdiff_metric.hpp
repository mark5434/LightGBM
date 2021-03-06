#ifndef LIGHTGBM_METRIC_TOPAVGDIFF_METRIC_HPP_
#define LIGHTGBM_METRIC_TOPAVGDIFF_METRIC_HPP_
#include <LightGBM/metric.h>

#include <LightGBM/utils/common.h>
#include <LightGBM/utils/log.h>

#include <LightGBM/utils/openmp_wrapper.h>

#include <sstream>
#include <vector>

namespace LightGBM {

class TopavgdiffMetric:public Metric {
 public:
  explicit TopavgdiffMetric(const Config& config) {
    // get eval position
    eval_at_ = config.eval_at;
    // get number of threads
    #pragma omp parallel
    #pragma omp master
    {
      num_threads_ = omp_get_num_threads();
    }
  }

  ~TopavgdiffMetric() {
  }

  void Init(const Metadata& metadata, data_size_t num_data) override {
    for (auto k : eval_at_) {
      name_.emplace_back(std::string("topavgdiff@") + std::to_string(k));
    }
    num_data_ = num_data;
    // get label
    label_ = metadata.label();
    // get query boundaries
    query_boundaries_ = metadata.query_boundaries();
    if (query_boundaries_ == nullptr) {
      Log::Fatal("For MAP metric, there should be query information");
    }
    num_queries_ = metadata.num_queries();
    Log::Info("Total groups: %d, total data: %d", num_queries_, num_data_);
    // get query weights
    query_weights_ = metadata.query_weights();
    if (query_weights_ == nullptr) {
      sum_query_weights_ = static_cast<double>(num_queries_);
    } else {
      sum_query_weights_ = 0.0f;
      for (data_size_t i = 0; i < num_queries_; ++i) {
        sum_query_weights_ += query_weights_[i];
      }
    }
  }

  const std::vector<std::string>& GetName() const override {
    return name_;
  }

  double factor_to_bigger_better() const override {
    return 1.0f;
  }

  void CalAvgdiffAtK(std::vector<int> ks, const label_t* label,
                 const double* score, data_size_t num_data, std::vector<double>* out) const {
    // get sorted indices by score
    std::vector<data_size_t> sorted_idx;
    for (data_size_t i = 0; i < num_data; ++i) {
      sorted_idx.emplace_back(i);
    }
    std::stable_sort(sorted_idx.begin(), sorted_idx.end(),
                     [score](data_size_t a, data_size_t b) {return score[a] > score[b]; });

    double sum_label = 0.0f;
    data_size_t cur_left = 0;
    for (size_t i = 0; i < ks.size(); ++i) {
      data_size_t cur_k = static_cast<data_size_t>(ks[i]);
      if (cur_k > num_data) { cur_k = num_data; }
      for (data_size_t j = cur_left; j < cur_k; ++j) {
        data_size_t idx1 = sorted_idx[j];
        data_size_t idx2 = sorted_idx[num_data - j - 1];
        sum_label += label[idx1] - label[idx2];
      }
      (*out)[i] = sum_label / (cur_k * 2);
      cur_left = cur_k;
    }
  }
  std::vector<double> Eval(const double* score, const ObjectiveFunction*) const override {
    // some buffers for multi-threading sum up
    std::vector<std::vector<double>> result_buffer_;
    for (int i = 0; i < num_threads_; ++i) {
      result_buffer_.emplace_back(eval_at_.size(), 0.0f);
    }
    std::vector<double> tmp_map(eval_at_.size(), 0.0f);
    if (query_weights_ == nullptr) {
      #pragma omp parallel for schedule(guided) firstprivate(tmp_map)
      for (data_size_t i = 0; i < num_queries_; ++i) {
        const int tid = omp_get_thread_num();
        CalAvgdiffAtK(eval_at_, label_ + query_boundaries_[i],
                  score + query_boundaries_[i], query_boundaries_[i + 1] - query_boundaries_[i], &tmp_map);
        for (size_t j = 0; j < eval_at_.size(); ++j) {
          result_buffer_[tid][j] += tmp_map[j];
        }
      }
    } else {
      #pragma omp parallel for schedule(guided) firstprivate(tmp_map)
      for (data_size_t i = 0; i < num_queries_; ++i) {
        const int tid = omp_get_thread_num();
        CalAvgdiffAtK(eval_at_, label_ + query_boundaries_[i],
                  score + query_boundaries_[i], query_boundaries_[i + 1] - query_boundaries_[i], &tmp_map);
        for (size_t j = 0; j < eval_at_.size(); ++j) {
          result_buffer_[tid][j] += tmp_map[j] * query_weights_[i];
        }
      }
    }
    // Get final average
    std::vector<double> result(eval_at_.size(), 0.0f);
    for (size_t j = 0; j < result.size(); ++j) {
      for (int i = 0; i < num_threads_; ++i) {
        result[j] += result_buffer_[i][j];
      }
      result[j] /= sum_query_weights_;
    }
    return result;
  }

 private:
  /*! \brief Number of data */
  data_size_t num_data_;
  /*! \brief Pointer of label */
  const label_t* label_;
  /*! \brief Query boundaries information */
  const data_size_t* query_boundaries_;
  /*! \brief Number of queries */
  data_size_t num_queries_;
  /*! \brief Weights of queries */
  const label_t* query_weights_;
  /*! \brief Sum weights of queries */
  double sum_query_weights_;
  /*! \brief Evaluate position of Nmap */
  std::vector<data_size_t> eval_at_;
  /*! \brief Number of threads */
  int num_threads_;
  std::vector<std::string> name_;
};

}  // namespace LightGBM

#endif   // LIGHTGBM_METRIC_MAP_METRIC_HPP_

