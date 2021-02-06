
// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.16
// 说明： 该任务延时队列，只适用于节点上报数据时的任务队列
// 延时任务队列的功能很大一部分由其定义的数据结构决定 

#ifndef  _DATA_QUEUE_H    
#define  _DATA_QUEUE_H 
    
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
#define  MAX_DATA_QUEUE   3000 

//服务器应答所需的数据结构
//数据缓存处理
typedef struct data 
{
    uint16_t size;
    uint8_t  payload[1024];
}Data;

typedef struct data_node 
{
    Data    data;
    struct  data_node *next;  //构成链表的关键:相同的结构体中可含有该结构体指针

}Data_Node;

typedef struct data_queue
{
    Data_Node *front;  //指向队列首项的指针
    Data_Node *rear;   //指向队列尾项的指针
    int  items;   //队列中的个数
}Data_Queue;

/*操作：     初始化队列        */
/*前提条件：  pq指向一个队列    */
/*后置条件：  队列被初始化为空  */
void Data_InitializeQueue(Data_Queue *pq);

/*操作：      检查队列是否已满        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列已满则返回true,否则返回false */
bool Data_QueueIsFull(const Data_Queue *pq);

/*操作：      检查队列是否为空        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列为空则返回true,否则返回false */
bool Data_QueueIsEmpty(const Data_Queue *pq);

/*操作：      确定队列中的项数         */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件     返回队列中的项数        */
int Data_QueueItemCount(const Data_Queue *pq);

/*操作：      在队列末尾添加项        */
/*前提条件：   pq指向之前被初始化的队列 */
/*           item是要被添加在队列末尾的项 */
/*后置条件:   如果队列不为空，item将被添加在队列的末尾 */
/*           该函数返回true;否则，队列不改变，该函数返回false */
bool Data_EnQueue(Data data, Data_Queue *pq);

/*操作：      从队列的开头删除项        */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件:   如果队列不为空，队列首端的item将被拷贝到*pitem中 */
/*           并被删除，且函数返回true;                    */
/*           如果该操作使得队列为空，则重置队列为空         */
/*           如果队列在操作前为空，该函数返回false         */
bool Data_DeQueue(Data *pdata, Data_Queue *pq);

/*操作：      清空队列                */
/*前提条件:   pq指向之前被初始化的队列  */
/*后置条件：  队列被清空              */
void Data_EmptyTheQueue(Data_Queue *pq);

//列出队列中所有的节点
//自己添加的
void Data_List_Queue(Data_Queue *pq);

//插入排序
//不用排序
//void Data_Insertion_Sort(Data_Queue *pq);

//获得队列首元素
//自己添加的
int Data_GetFront_Queue(Data_Queue *pq,uint16_t *size,uint8_t *data);
//==============================================================================================================//

#endif

