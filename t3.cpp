/*'
条件变量、互斥量、计数C

生产者：
每个线程要先获得锁，保证它唯一执行，
然后判断是否满足条件（C小于Max），否则cond_wait等待条件
生产
释放锁

消费者：
获得锁
判断条件（C大于0），否则cond_wait等待条件
消费
释放锁
'*/

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

using namespace std;

pthread_mutex_t mut;
pthread_cond_t cond;
int cnt = 3;
int MAX = 10;

void* producer(void* arg){
    for(int i = 0; i < MAX; i++){
        pthread_mutex_lock(&mut);

        cnt++;
        printf("produce %d cnt:%d\n", i, cnt);
        pthread_cond_signal(&cond);

        pthread_mutex_unlock(&mut);
        sleep(2);
    }
    pthread_exit(0);
}

void* consummer(void* arg){
    for(int i = 0; i < MAX; i++){
        pthread_mutex_lock(&mut);

        if(cnt <= 0){
            pthread_cond_wait(&cond, &mut);
        }
        cnt--;
        printf("consum %d cnt:%d\n", i, cnt);

        pthread_mutex_unlock(&mut);
        sleep(1);
    }
    pthread_exit(0);
}

int main(){
    pthread_mutex_init(&mut, NULL);
    pthread_cond_init(&cond, NULL);

    pthread_t p1, p2;
    pthread_create(&p1, NULL, producer, NULL);
    pthread_create(&p2, NULL, consummer, NULL);

    pthread_join(p1, 0);
    pthread_join(p2, 0);

    pthread_mutex_destroy(&mut);
    pthread_cond_destroy(&cond);
    return 0;
}