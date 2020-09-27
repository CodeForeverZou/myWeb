/*"
生产、消费者
FULL = 0
EMPTY = 10

creater,
P(Empty)

c

V(FULL)

consumer,
P(FULL)

s

V(Empty)

信号量（条件变量）
互斥量：访问FULL
"*/

#include <mutex>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>

sem_t FULL;
sem_t EMPTY;
pthread_mutex_t mut;
int Buff[10];
int MAX = 10;


void* consumer(void* args){
    for(int i = 0; i < MAX; i++){
        sem_wait(&FULL);
        // Buff
        pthread_mutex_lock(&mut);
        printf("consume %d item\n", i);
        pthread_mutex_unlock(&mut);
        //
        sem_post(&EMPTY);
        sleep(2);
    }
    pthread_exit(0);
}

void* producer(void* args){
    for(int i = 0; i < MAX; i++){
        sem_wait(&EMPTY);
        // Buff
        pthread_mutex_lock(&mut);
        printf("produce %d item\n", i);
        pthread_mutex_unlock(&mut);
        //
        sem_post(&FULL);
        sleep(1);
    }
    pthread_exit(0);
}


int main(){
    sem_init(&FULL, 0, 0);
    sem_init(&EMPTY, 0, 10);
    pthread_mutex_init(&mut, NULL);

    pthread_t p1, p2;
    pthread_create(&p1, NULL, consumer, NULL);
    pthread_create(&p2, NULL, producer, NULL);

    pthread_join(p1, 0);
    pthread_join(p2, 0);
    pthread_mutex_destroy(&mut);
    sem_destroy(&FULL);
    sem_destroy(&EMPTY);

    return 0;
}
