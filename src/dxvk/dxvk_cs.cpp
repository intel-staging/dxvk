#include "dxvk_cs.h"

namespace dxvk {
  
  DxvkCsChunk::DxvkCsChunk() {
    
  }
  
  
  DxvkCsChunk::~DxvkCsChunk() {
    for (size_t i = 0; i < m_commandCount; i++)
      m_commandList[i]->~DxvkCsCmd();
  }
  
  
  void DxvkCsChunk::executeAll(DxvkContext* ctx) {
    for (size_t i = 0; i < m_commandCount; i++) {
      m_commandList[i]->exec(ctx);
      m_commandList[i]->~DxvkCsCmd();
    }
    
    m_commandCount  = 0;
    m_commandOffset = 0;
  }
  
  
  DxvkCsThread::DxvkCsThread(const Rc<DxvkContext>& context)
  : m_context(context), m_thread([this] { threadFunc(); }) {
    
  }
  
  
  DxvkCsThread::~DxvkCsThread() {
    { std::unique_lock<std::mutex> lock(m_mutex);
      m_stopped.store(true);
    }
    
    m_condOnAdd.notify_one();
    m_thread.join();
  }
  
  
  void DxvkCsThread::dispatchChunk(Rc<DxvkCsChunk>&& chunk) {
    { std::unique_lock<std::mutex> lock(m_mutex);
      m_chunksQueued.push(std::move(chunk));
      m_chunksPending += 1;
      
      // If a large number of chunks are queued up, wait for
      // some of them to be processed in order to avoid memory
      // leaks, stuttering, input lag and similar issues.
      if (m_chunksPending >= MaxChunksInFlight) {
        m_condOnSync.wait(lock, [this] {
          return (m_chunksPending < MaxChunksInFlight / 2)
              || (m_stopped.load());
        });
      }
    }
    
    // Wake CS thread
    m_condOnAdd.notify_one();
  }
  
  
  void DxvkCsThread::synchronize() {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    m_condOnSync.wait(lock, [this] {
      return m_chunksPending == 0;
    });
  }
  
  
  void DxvkCsThread::threadFunc() {
    Rc<DxvkCsChunk> chunk;
    
    while (!m_stopped.load()) {
      { std::unique_lock<std::mutex> lock(m_mutex);
        if (chunk != nullptr) {
          m_chunksPending -= 1;
          m_condOnSync.notify_one();
        }
        
        m_condOnAdd.wait(lock, [this] {
          return m_stopped.load() || (m_chunksQueued.size() != 0);
        });
        
        if (m_chunksQueued.size() != 0) {
          chunk = std::move(m_chunksQueued.front());
          m_chunksQueued.pop();
        } else {
          chunk = nullptr;
        }
      }
      
      if (chunk != nullptr)
        chunk->executeAll(m_context.ptr());
    }
  }
  
}