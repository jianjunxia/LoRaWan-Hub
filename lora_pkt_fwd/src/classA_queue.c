#include "classA_queue.h"

/*------------------------------------------队列函数实现-------------------------------------------*/

/* 局部函数，把项拷贝到节点中 */
static void CopyToNode(class_A data, class_a_Node *pn)
{

    pn->classa.value_len        = data.value_len;  

    /* 拷贝valueBuf的值 */
    memcpy(pn->classa.valueBuf,data.valueBuf,data.value_len);

    /* deveui 8 bytes */
    memcpy(pn->classa.deveui,data.deveui,8);

}

/* 局部函数，把节点拷贝到项中 */
static void CopyToItem(class_a_Node *pn, class_A *pi)
{
    *pi = pn->classa;
}

/*操作：     初始化队列        */
/*前提条件：  pq指向一个队列    */
/*后置条件：  队列被初始化为空   */
void ClassA_InitializeQueue(class_a_Queue *pq)
{
    /* 初始化首尾指针为空 */
    pq->front = NULL;
    pq->rear  = NULL;
    pq->items = 0;

}

/*操作：      检查队列是否已满        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列已满则返回true,否则返回false */
bool ClassA_QueueIsFull(const class_a_Queue *pq)
{
    return pq->items == MAX_CLASSA_QUEUE;
}

/*操作：      检查队列是否为空        */
/*前提条件：   pq指向之前被初始化的队列 */
/*后置条件    如果队列为空则返回true,否则返回false */
bool ClassA_QueueIsEmpty(const class_a_Queue *pq)
{
    return pq->items == 0;
}

/*操作：       确定队列中的项数         */
/*前提条件：   pq指向之前被初始化的队列  */
/*后置条件     返回队列中的项数        */
int ClassA_QueueItemCount(const class_a_Queue *pq)
{
    return pq->items;
}


/*
    在队列末尾添加项
    
    step1: 创建1个新的节点
    step2: 把项拷贝到节点中
    step3: 设置节点的next为NULL，表明该节点是最后1个节点
    step4：设置当前尾节点的next指针指向新节点，把新节点链接到队列中
    step5: 把rear指针指向新节点，以便找到最后的节点
    step6: 项目 +1

*/
bool ClassA_EnQueue(class_A data, class_a_Queue *pq)
{
    class_a_Node *pnew;

    /* 检查队列是否已满 */
    if ( ClassA_QueueIsFull(pq))
                return false;
    
    /* 给新节点分配空间 */
    pnew = (class_a_Node*)malloc(sizeof(class_a_Node));
    
    if ( pnew == NULL){
            
            fprintf(stderr,"Unable to allocate memory!\n");
            return false;
    }

    /* 把项拷贝到节点中 */
    CopyToNode(data,pnew);

    /* 设置当前节点为null，表明这是最后1个节点 */
    pnew->next = NULL;

    /* 如果该节点是第1个入队列，则节点位于队列的首端 */
    if ( ClassA_QueueIsEmpty(pq))
    {    
            pq->front = pnew; 
    }
    else
    {       
            /* 备注：这里pq->rear->next 和 pq->next 构成了链表 */

            /*
                1: pq->front = pnew1;
                   pq->rear  = pnew1;

                2: pq->rear->next = pnew1->next =  pnew2;
                   pq->rear       = pnew2;
                
                3: pq->rear->next = pnew2->next =  pnew3;
                    pq->rear      = pnew3;
                 ... 
            */
            pq->rear->next = pnew; /* 链接到尾端 */
    }

    pq->rear = pnew;
    /* 项加1 */
    pq->items++;
    return true;

}

/*
    从队首删除项

    step1： 把项拷贝到给定变量中
    step2:  释放空出的节点使用的内存空间
    step3:  重置首指针指向队列中的下一项
    step4:  如果删除的是最后1项，则把首指针和尾指针都重置为NULL
    step5:  项数减1
*/
bool ClassA_DeQueue(class_A *ptask, class_a_Queue *pq)
{

    class_a_Node *pt;
    
    if ( ClassA_QueueIsEmpty(pq))
            return false;
    
    /* 节点拷贝到项中 */
    CopyToItem(pq->front,ptask);

    pt = pq->front;

    /* 重置首指针指向队列中的下一项 */
    pq->front = pq->front->next;

    /* 释放空节点的内存空间 */
    free(pt);

    /* 项数减1 */
    pq->items--;

    if ( pq->items == 0)
            pq->rear = NULL;
    
    return true;
}

/*操作：      清空队列                */
/*前提条件:   pq指向之前被初始化的队列  */
/*后置条件：  队列被清空              */
void ClassA_EmptyTheQueue(class_a_Queue *pq)
{
    class_A dummy;
    while (!ClassA_QueueIsEmpty(pq))
    {
        ClassA_DeQueue(&dummy, pq);
    }
}

/*
    列出队列中所有的节点
    自己添加的
*/
void ClassA_List_Queue(class_a_Queue *pq)
{
        /* 暂时不用实现 */
}

/*
    获得队列首元素
    自己添加的
*/
int ClassA_GetFront_Queue(class_a_Queue *pq, uint16_t *value_len, uint8_t *valueBuf, uint8_t *deveui)
{
    if ( ClassA_QueueIsEmpty(pq)) 
    {
            /* 首指针置为null */
            pq->front = NULL;
            return 1;
    }
    else
    {
            /* 提取有用数据 */
            *value_len = pq->front->classa.value_len;
            memcpy ( valueBuf,pq->front->classa.valueBuf,pq->front->classa.value_len);
            memcpy ( deveui,pq->front->classa.deveui,8);
            
            return 0;
    }
        
}