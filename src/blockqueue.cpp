// #include "blockqueue.h"
// template<class T>
//   BlockDeque<T>::BlockDeque(size_t MaxCapacity):capacity_(MaxCapacity){ }


// template<class T>
//   BlockDeque<T>::~BlockDeque(){
//     Close();
//   }

//  template<class T>
//  void  BlockDeque<T>::Close(){
//        {
//         std::lock_guard<std::mutex> lock(mutex_);
//         deq.clear();
//         isClose_=true;
//        }
//        condConsumer_.notify_all();
//        condProducer_.notify_all();
//  }
// template<class T>
//  void  BlockDeque<T>::clear(){
//      std::lock_guard<std::mutex> lock(mutex_);
//      deq.clear();
//  }
// template<class T>
//  bool  BlockDeque<T>::empty(){
//      std::lock_guard<std::mutex> lock(mutex_);
//     return  deq.empty();
//  }

// template<class T>
//  bool  BlockDeque<T>::full(){
//     std::lock_guard<std::mutex> locker(mutex_);
//     return deq.size() >= capacity_;
//  }
// template<class T>
//  size_t  BlockDeque<T>::size(){
//      std::lock_guard<std::mutex> lock(mutex_);
//     return  deq.size();
//  }
// template<class T>
//  size_t  BlockDeque<T>::capacity(){
//      std::lock_guard<std::mutex> lock(mutex_);
//       return  capacity_;
//  }
// template<class T>
//  T  BlockDeque<T>::front(){
//      std::lock_guard<std::mutex> lock(mutex_);
//      return  deq.front();
//  }
// template<class T>
//  T  BlockDeque<T>::back(){
//      std::lock_guard<std::mutex> lock(mutex_);
//      return deq.back();
//  }
// template<class T>
// void  BlockDeque<T>::push_back(const T &item){
//      std::unique_lock<std::mutex> lock(mutex_);
//      condProducer_.wait(lock,[this](){
//        return deq.size()<capacity_;
//      });
//      if(isClose_){
//         return ;
//      }
//      deq.push_back(item);
//      condConsumer_.notify_one();
// }
// template<class T>
// void  BlockDeque<T>::push_front(const T &item){
//      std::unique_lock<std::mutex> lock(mutex_);
//     condProducer_.wait(lock, condProducer_.wait(lock,[this](){
//        return deq.size()<capacity_;
//      }));
//      if(isClose_){
//         return ;
//      }
//      deq.push_front(item);
//      condConsumer_.notify_one();
// }
// template<class T>
//  bool  BlockDeque<T>::pop(T &item){
//     std::unique_lock<std::mutex> lock(mutex_);
//     condConsumer_.wait(lock,[this](){
//           return  !deq.empty();
//     });
//      if(isClose_){
//         return false;
//      }
//     item=deq.front();
//     deq.pop_front();
//     condProducer_.notify_one();
//     return  true;
//  }
// template<class T>
//  bool  BlockDeque<T>::pop(T &item, int timeout){
//        std::unique_lock<std::mutex> lock(mutex_);
//        while (empty()) {//std::chrono::seconds(timeout）专门用于计时的，wait_for会返回状态有·timeout和no_timeout
//               if(condConsumer_.wait_for(lock, std::chrono::seconds(timeout))==std::cv_status::timeout){
//                              return  false;
//               }
//               if(isClose_){
//                  return false;
//               }
//        }
//     item=deq.front();
//     deq.pop_front();
//     condProducer_.notify_one();
//     return  true;
//  }
//  template<class T>
// void  BlockDeque<T>::flush(){
//     condConsumer_.notify_one();
// }