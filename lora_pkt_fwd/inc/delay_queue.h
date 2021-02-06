// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.11
// 说明： 该任务延时队列，只适用于节点join时的延时任务队列
// 延时任务队列的功能很大一部分由其定义的数据结构决定 


#ifndef  _DELAY_QUEUE_H    
#define  _DELAY_QUEUE_H
    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>        /* bool type */
#include <pthread.h>
#include <semaphore.h>
#define   uint8_t      unsigned char
#define   uint16_t     unsigned short int
#define   uint32_t     unsigned int

//宏定义，调试配置代码使用
#define   _DEBUG_CONF_

#ifdef   _DEBUG_CONF_
         #define  DEBUG_CONF(fmt,args...)   fprintf(stderr,"[%d]: "fmt,__LINE__,##args)
#else
        #define   DEBUG_CONF(fmt,args...)
#endif


//==================================延时队列调度算法====================================//
// *  Lierda |  Senthink                           ************
// *  author: jianjun_xia                          ***********   
// *  data  : 2018.9.13                            ***********  
 //*                                               ********** 

#define  MAXQUEUE   5000 //队列中最大的数

/**
 *  join request 入网所需数据结构 
 *  update:2019.3.8
 *  增加freq_hz、coderate、datarate元素
 * 
 */
typedef struct item 
{
    /* 本地时间戳 */
    unsigned long      T1;

    /* 延时属性 */
    unsigned int       delay;

    /* 出队列的时间 */
    unsigned long      Qut_Queue_Delay; 
    
    /* deveui */
    unsigned char      deveui[8];
    
    /* appeui */
    unsigned char      appeui[8];
    
    /* devnonce */
    unsigned short int devnonce;

    /* sx1301接收到数据包时候的内部时间戳 timestamp */   
    uint32_t count_us;

    /* 真实的下发延时时间,默认5s */
    uint32_t real_delay;
    
    /* 下发时间戳: count_us + real_delay */
    uint32_t tx_count_us; 

    /* 节点的频率信息 */
    uint32_t freq_hz;

    /* 节点的编码率信息 */
    uint8_t coderate;

    /* 节点的速率信息 */
    uint32_t datarate;

}Item; 

/* 节点信息 */
/* 构成链表的关键:结构体中含有指向自身的指针 */
typedef struct node 
{
    Item    item;
    struct  node *next;

}Node;

/* 队列信息 */
typedef struct queue
{
    Node *front;  /* 指向队列首项的指针 */
    Node *rear;   /* 指向队列尾项的指针 */
    int  items;   /* 队列中的个数      */
 }Queue;

/*操作：     初始化队列        */
/*前提条件：  pq指向一个队列    */
/*后置条件：  队列被初始化为空  */
void InitializeQueue(Queue *pq);

/*操作：      检查队列是否已满        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列已满则返回true,否则返回false */
bool QueueIsFull(const Queue *pq);

/*操作：      检查队列是否为空        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列为空则返回true,否则返回false */
bool QueueIsEmpty(const Queue *pq);

/*操作：      确定队列中的项数         */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件     返回队列中的项数        */
int QueueItemCount(const Queue *pq);

/*操作：      在队列末尾添加项        */
/*前提条件：   pq指向之前被初始化的队列 */
/*           item是要被添加在队列末尾的项 */
/*后置条件:   如果队列不为空，item将被添加在队列的末尾 */
/*           该函数返回true;否则，队列不改变，该函数返回false */
bool EnQueue(Item item, Queue *pq);

/*操作：      从队列的开头删除项        */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件:   如果队列不为空，队列首端的item将被拷贝到*pitem中 */
/*           并被删除，且函数返回true;                    */
/*           如果该操作使得队列为空，则重置队列为空         */
/*           如果队列在操作前为空，该函数返回false         */
bool DeQueue(Item *pitem, Queue *pq);

/*操作：      清空队列                */
/*前提条件:   pq指向之前被初始化的队列  */
/*后置条件：  队列被清空              */
void EmptyTheQueue(Queue *pq);

/**
 * 列出队列中所有的节点
 * 自己添加的
 */ 
void List_Queue(Queue *pq);

/**
 * 冒泡排序
 * 自己添加的
 * 算法复杂度高
 * 已弃用 
 */
void Bubble_Sort(Queue *pq);

/**
 * 插入排序
 * 自己添加的
 * 算法复杂度低
 */ 
void Insertion_Sort(Queue *pq);

/**
 * 获得队列首元素 
 * update:2019.3.8
 * 增加获取freq_hz、codedate、datarate信心
 * 省略 join accept 线程传参
 * 
 */
int GetFront_Queue(Queue *pq,unsigned long *quit_time,uint8_t  *deveui,
                                                      uint16_t *dev_nonce,
                                                      uint8_t  *appeui,
                                                      uint32_t *tx_count_us,
                                                      uint32_t *freq_hz,
                                                      uint8_t  *coderate,
                                                      uint32_t *datarate);

//============================================================================================//

#endif