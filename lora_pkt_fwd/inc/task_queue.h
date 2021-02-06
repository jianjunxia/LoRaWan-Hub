// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.16
// 说明： 该任务延时队列，只适用于节点上报数据时的延时任务队列
// 延时任务队列的功能很大一部分由其定义的数据结构决定 


#ifndef  _TASK_QUEUE_H    
#define  _TASK_QUEUE_H 
    
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

/* 队列中最大的数据量 */
#define  MAX_TASK_QUEUE   3000

/* unconfirmed/confirmed ack data所需的数据结构 */
typedef struct task 
{
    unsigned long      T1;    //UTC时间戳
    uint32_t           delay; //延时属性
    unsigned long      Qut_Queue_Delay; //出队列的时间
    uint32_t           freq_hz;      //freq
    uint8_t            bandwidth;    //bandwidth
    uint32_t           datarate;     //datarate
    uint8_t            coderate;     //coderate
    uint16_t           size;         //size
    uint8_t            payload[500]; //载荷缓存区
    
    uint32_t           count_us;     //接收到数据的时间戳 单位:us
    uint32_t           real_delay;   //节点真实的延时属性 单位:us 
    uint32_t           tx_count_us;  //节点下发的时间戳   单位:us         

}Task;

typedef struct task_node 
{
    Task    task;
    struct  task_node *next;  //构成链表的关键:相同的结构体中可含有该结构体指针

}Task_Node;

typedef struct task_queue
{
    Task_Node *front;  //指向队列首项的指针
    Task_Node *rear;   //指向队列尾项的指针
    int  items;   //队列中的个数
}Task_Queue;

/*操作：     初始化队列        */
/*前提条件：  pq指向一个队列    */
/*后置条件：  队列被初始化为空  */
void Task_InitializeQueue(Task_Queue *pq);

/*操作：      检查队列是否已满        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列已满则返回true,否则返回false */
bool Task_QueueIsFull(const Task_Queue *pq);

/*操作：      检查队列是否为空        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列为空则返回true,否则返回false */
bool Task_QueueIsEmpty(const Task_Queue *pq);

/*操作：      确定队列中的项数         */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件     返回队列中的项数        */
int Task_QueueItemCount(const Task_Queue *pq);

/*操作：      在队列末尾添加项        */
/*前提条件：   pq指向之前被初始化的队列 */
/*           item是要被添加在队列末尾的项 */
/*后置条件:   如果队列不为空，item将被添加在队列的末尾 */
/*           该函数返回true;否则，队列不改变，该函数返回false */
bool Task_EnQueue(Task task, Task_Queue *pq);

/*操作：      从队列的开头删除项        */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件:   如果队列不为空，队列首端的item将被拷贝到*pitem中 */
/*           并被删除，且函数返回true;                    */
/*           如果该操作使得队列为空，则重置队列为空         */
/*           如果队列在操作前为空，该函数返回false         */
bool Task_DeQueue(Task *ptask, Task_Queue *pq);

/*操作：      清空队列                */
/*前提条件:   pq指向之前被初始化的队列  */
/*后置条件：  队列被清空              */
void Task_EmptyTheQueue(Task_Queue *pq);

//列出队列中所有的节点
//自己添加的
void Task_List_Queue(Task_Queue *pq);

//插入排序
void Task_Insertion_Sort(Task_Queue *pq);

//获得队列首元素
//自己添加的
int Task_GetFront_Queue(Task_Queue *pq,unsigned long *quit_time,uint32_t *freq_hz,uint8_t *bandwidth,uint32_t *datarate,uint8_t *coderate,uint16_t *size,uint8_t *payload,uint32_t *tx_count_us);
//==============================================================================================================//

#endif














