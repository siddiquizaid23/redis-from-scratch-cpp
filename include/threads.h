#pragma once
#include <stddef.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <deque>

struct Work
{
    void (*f)(void *) = NULL;
    void *arg = NULL;
};
struct ThreadPool{
    std::vector <pthread_t> threads;

    std::deque<Work> queue;
    pthread_mutex_t mu;
    pthread_cond_t not_empty;

};
 static void *worker(void *arg){
    ThreadPool *pool = (ThreadPool *)arg;
    while (true)
    {
        pthread_mutex_lock(&pool->mu);

        while (pool->queue.empty()) 
        {
            pthread_cond_wait(&pool->not_empty,&pool->mu);
        }
        Work w = pool->queue.front();
        pool->queue.pop_front();
        pthread_mutex_unlock(&pool->mu);
        w.f(w.arg);
        
    }
    return NULL;
    
 }
 static void thread_pool_init(ThreadPool *pool,size_t num_threads){
    pthread_mutex_init(&pool->mu,NULL);
    pthread_cond_init(&pool->not_empty,NULL);

    pool->threads.resize(num_threads);
    for (size_t i = 0; i < num_threads; i++)
    {
        int rv = pthread_create(&pool->threads[i],NULL,worker,pool);
    
    if(rv!= 0){
        fprintf(stderr,"pthread_create failed: %d\n",rv);
        abort();
    }
}
 }

 [[maybe_unused]] static void thread_pool_queue(ThreadPool *pool,void(*f)(void *),void *arg){
    Work w;
    w.f =f ;
    w.arg = arg;
    pthread_mutex_lock(&pool->mu);
    pool->queue.push_back(w);
    pthread_mutex_unlock(&pool->mu);
    pthread_cond_signal(&pool->not_empty);
 }