#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>
#include <assert.h>

class Sem
{
private:
    sem_t m_sem;
public:
    Sem(int sem){sem_init(&m_sem, 1, sem);}
    ~Sem(){sem_destroy(&m_sem);}
    bool P(){return sem_wait(&m_sem) == 0;}
    bool V(){return sem_post(&m_sem) == 0;}
};

Sem Full(0);
Sem Empty(5);

void* worker(void* arg){
    int n = 5;
    while(n){
        Empty.P();
        printf("create 1\n");
        sleep(1);
        // assert(Full.V() != true);
        Full.V();
        n--;
    }
}

void* comsumer(void* arg){
    int n = 5;
    while(n){
        Full.P();
        printf("consume 1\n");
        sleep(0.5);
        // assert(Empty.V() != true);
        Empty.V();
        n--;
    }
}

int main(){
    printf("1\n");
    pthread_t id1;
    pthread_t id2;
    pthread_create(&id1, NULL, worker, NULL);   // 创建线程
    pthread_create(&id2, NULL, comsumer, NULL);
    pthread_join(id1, NULL);    // 等待回收线程
    pthread_join(id2, NULL);
    printf("2\n");
    return 0;
}