/*
    @ lierda |      senthink
    @ author：      jianjun_xia
    @ update：      2019-06-13
    @ function：    用于调试class a设备的存储队列
    @ comment：     web端调试class a设备时，hub需先暂存web端下发的数据，等到有该节点上报数据时，才下发。  
 
 */ 

#ifndef     _CLASS_A_QUEUE_H
#define     _CLASS_A_QUEUE_H

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdbool.h>

#define     uint8_t         unsigned char
#define     uint16_t        unsigned short int
#define     uint32_t        unsigned int

/* 宏定义，调试配置代码使用 */
#define     _DEBUG_CONF_

#ifdef      _DEBUG_CONF_
            #define     DEBUG_CONF(fmt,args...) fprintf(stderr,"[%d]: "fmt,__LINE__,##args)
#else
           #define      DEBUG_CONF(fmt,args...)  
#endif

/* 队列中最大的数据量 */
#define     MAX_CLASSA_QUEUE    3000

/* class a 设备的数据结构 */
/* 队列的项 */
typedef struct class_a
{
    uint16_t    value_len;          /* value数据的长度 */
    uint8_t     valueBuf[1024];     /* value数据存储  */ 
    uint8_t     deveui[8];          /* deveui信息    */            

}class_A;

/* 队列的节点 */
typedef struct  class_a_node 
{
    class_A     classa;   
    struct      class_a_node *next;   /* 构成链表的关键：含有指向自身的结构体指针 */   

}class_a_Node;

typedef struct  class_a_queue
{

    class_a_Node   *front;  /* 指向队列首项的指针 */
    class_a_Node   *rear;   /* 指向队列尾项的指针 */
    int  items;             /* 队列中的个数      */

}class_a_Queue;

/*操作：     初始化队列        */
/*前提条件：  pq指向一个队列    */
/*后置条件：  队列被初始化为空  */
void ClassA_InitializeQueue(class_a_Queue *pq);

/*操作：      检查队列是否已满        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列已满则返回true,否则返回false */
bool ClassA_QueueIsFull(const class_a_Queue *pq);

/*操作：      检查队列是否为空        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列为空则返回true,否则返回false */
bool ClassA_QueueIsEmpty(const class_a_Queue *pq);

/*操作：      确定队列中的项数         */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件     返回队列中的项数        */
int ClassA_QueueItemCount(const class_a_Queue *pq);

/*操作：      在队列末尾添加项        */
/*前提条件：   pq指向之前被初始化的队列 */
/*           item是要被添加在队列末尾的项 */
/*后置条件:   如果队列不为空，item将被添加在队列的末尾 */
/*           该函数返回true;否则，队列不改变，该函数返回false */
bool ClassA_EnQueue(class_A data, class_a_Queue *pq);

/*操作：      从队列的开头删除项        */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件:   如果队列不为空，队列首端的item将被拷贝到*pitem中 */
/*           并被删除，且函数返回true;                    */
/*           如果该操作使得队列为空，则重置队列为空         */
/*           如果队列在操作前为空，该函数返回false         */
bool ClassA_DeQueue(class_A *ptask, class_a_Queue *pq);

/*操作：      清空队列                */
/*前提条件:   pq指向之前被初始化的队列  */
/*后置条件：  队列被清空              */
void ClassA_EmptyTheQueue(class_a_Queue *pq);

/*
    列出队列中所有的节点
    自己添加的
*/
void ClassA_List_Queue(class_a_Queue *pq);

/*
    获得队列首元素
    自己添加的
*/
int ClassA_GetFront_Queue(class_a_Queue *pq, uint16_t *value_len, uint8_t *valueBuf, uint8_t *deveui);

#endif