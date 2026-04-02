#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

/**
 * @brief 멀티스레드 환경에서 안전하게 사용할 수 있는 큐 래퍼 클래스
 *
 * Check (empty)와 Action (front/pop) 사이의 레이스 컨디션을 방지하기 위해
 * wait_and_pop 방식을 제공합니다.
 */
template <typename T> class ThreadSafeQueue {
public:
  explicit ThreadSafeQueue(size_t capacity = 0) : capacity_(capacity) {}

  /**
   * @brief 데이터를 큐에 삽입 (용량 초과 시 가장 오래된 데이터 버림)
   */
  void push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 용량 제한이 있고 꽉 찼다면 가장 오래된 데이터 하나 제거
    if (capacity_ > 0 && queue_.size() >= capacity_) {
      queue_.pop();
    }

    queue_.push(std::move(value));
    cond_.notify_one();
  }

  /**
   * @brief 데이터가 들어올 때까지 대기했다가 꺼냄 (Interruptible Block)
   * @param value 꺼낸 데이터를 저장할 참조
   * @param stop_condition 중단 조건 (true일 경우 대기 중단)
   * @return 데이터를 성공적으로 꺼냈으면 true, 중단되었으면 false
   */
  template <typename Predicate>
  bool wait_and_pop(T &value, Predicate stop_condition) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this, stop_condition] {
      return !queue_.empty() || stop_condition();
    });

    if (queue_.empty()) {
      return false;
    }

    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  /**
   * @brief 대기하지 않고 데이터가 있으면 꺼냄 (Non-block)
   * @return 성공 여부
   */
  bool try_pop(T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  /**
   * @brief 큐가 비어있는지 확인
   */
  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }

  /**
   * @brief 큐의 현재 크기 확인
   */
  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  /**
   * @brief 모든 대기 중인 스레드를 깨움 (종료 시 사용)
   */
  void notify_all() { cond_.notify_all(); }

  /**
   * @brief 큐 비우기
   */
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
      queue_.pop();
    }
  }

private:
  mutable std::mutex mutex_;
  std::queue<T> queue_;
  std::condition_variable cond_;
  size_t capacity_;
};

#endif // THREAD_SAFE_QUEUE_H
