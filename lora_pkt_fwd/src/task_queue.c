#include "task_queue.h"

//==================================队列函数实现===================================//
//局部函数
//把项拷贝到节点中
//初始化队列
static void CopyToNode(Task task, Task_Node *pn)
{
    pn->task.delay           = task.delay;
    pn->task.Qut_Queue_Delay = task.Qut_Queue_Delay;
    pn->task.T1              = task.T1;
    pn->task.freq_hz         = task.freq_hz;
    pn->task.bandwidth       = task.bandwidth;
    pn->task.datarate        = task.datarate;
    pn->task.coderate        = task.coderate;
    pn->task.size            = task.size;
    pn->task.count_us        = task.count_us;
    pn->task.real_delay      = task.real_delay;
    pn->task.tx_count_us     = task.tx_count_us;
    memcpy(pn->task.payload,task.payload,task.size);
}
//局部函数，把节点拷贝到项中
static void CopyToItem(Task_Node *pn, Task *pi)
{
    pi->delay           = pn->task.delay;
    pi->Qut_Queue_Delay = pn->task.Qut_Queue_Delay;
    pi->T1              = pn->task.T1;
    pi->freq_hz         = pn->task.freq_hz;
    pi->bandwidth       = pn->task.bandwidth;
    pi->datarate        = pn->task.datarate;
    pi->coderate        = pn->task.coderate;
    pi->size            = pn->task.size;
    pi->count_us        = pn->task.count_us;
    pi->real_delay      = pn->task.real_delay;
    pi->tx_count_us     = pn->task.tx_count_us;
    memcpy(pi->payload,pn->task.payload,pn->task.size); 
}

void Task_InitializeQueue(Task_Queue *pq)
{
    pq->front = pq->rear = NULL;
    pq->items = 0;
}

//  检查队列是否已满        
bool Task_QueueIsFull(const Task_Queue *pq)
{
    return pq->items == MAX_TASK_QUEUE; //检查队列是否已满
}

//  检查队列是否为空        
bool Task_QueueIsEmpty(const Task_Queue *pq)
{
    return pq->items == 0; //检查队列项数是否为0    
}

//  返回队列中的项数        
int Task_QueueItemCount(const Task_Queue *pq)
{
    return pq->items; 
}

//在队列末尾添加项
//1:创建一个新的节点
//2:把项拷贝到节点中
//3:设置节点的next指针为NULL，表明该节点是最后一个节点；
//4:设置当前尾节点的next指针指向新节点，把新节点链接到队列中；
//5:把rear指针指向新节点，以便找到最后的节点；
//6：项目 +1        
bool Task_EnQueue(Task task, Task_Queue *pq)
{
    Task_Node *pnew;

    //检查队列是否已满
    if(Task_QueueIsFull(pq))
    {
        return false;
    }
    //给新节点分配空间
    pnew = (Task_Node*)malloc(sizeof(Task_Node));
    if(pnew == NULL)
    {
        fprintf(stderr,"Unable to allocate memory!\n");
    }
    CopyToNode(task,pnew);  //把项拷贝到节点中
    pnew->next = NULL;      //设置当前节点的next为null,表明这是最后一个节点

    if(Task_QueueIsEmpty(pq))    //如果该节点是第一个入队的节点，则该节点位于队列的首端
    {
        pq->front = pnew;   //项位于队列的首端
    }
    else                    //否则链接到队尾  
    {
        pq->rear->next = pnew;  //链接到队列尾端
    }
    pq->rear = pnew;  //记录队列尾端的位置
    pq->items++;

    return true;      
}

// 从队列的开头删除项
//1:把项拷贝到给定的变量中；
//2:释放空出的节点使用的内存空间；
//3:重置首指针指向队列中的下一个项；
//4:如果删除最后一项，把首指针和尾指针都重置为NULL；
//5:项数-1；        
bool Task_DeQueue(Task *ptask, Task_Queue *pq)
{
    Task_Node *pt;

    if(Task_QueueIsEmpty(pq))
    {
        return false;
    }
    CopyToItem(pq->front,ptask);
    pt = pq->front;               
    pq->front = pq->front->next;  //重置首指针指向队列中的下一项
    free(pt);       //释放空节点的内存空间
    pq->items--;    //项目数-1
    if(pq->items == 0 )
    {
        pq->rear = NULL;
    }
    return true;
}

// 清空队列                
void Task_EmptyTheQueue(Task_Queue *pq)
{
    Task    dummy;
    while(!Task_QueueIsEmpty(pq))
    {
        Task_DeQueue(&dummy,pq);

    }
}

//列出队列中所有的节点
//自己添加的
void Task_List_Queue(Task_Queue *pq)
{
    Task_Node *p_buff;
    int index_buff;

    //记录队列首地址
    p_buff = pq->front;
    //记录队列项目数
    index_buff = pq->items;

    DEBUG_CONF(" list queue: %d\n",pq->items);
    if(Task_QueueIsEmpty(pq))
    {
        DEBUG_CONF("queue is not exit\n");
    }
    else
    {
        DEBUG_CONF("queue: \n");
        while(pq->items != 0)
        {
            DEBUG_CONF("node->T1:       %lu\n",pq->front->task.T1);
            DEBUG_CONF("node->delay:    %d\n" ,pq->front->task.delay);
            DEBUG_CONF("node->out_time: %lu\n",pq->front->task.Qut_Queue_Delay);
            DEBUG_CONF("node->freq:      %d\n",pq->front->task.freq_hz);
            DEBUG_CONF("node->bandwidth: %d\n",pq->front->task.bandwidth);
            DEBUG_CONF("node->datarate:  %d\n",pq->front->task.datarate);
            DEBUG_CONF("node->coderate:  %d\n",pq->front->task.coderate);
            pq->items--; 
            pq->front = pq->front->next;
            DEBUG_CONF("pq->items:%d\n",pq->items);  
        }
        DEBUG_CONF("end\n");
    }
    //恢复首地址
    pq->front = p_buff;
    pq->items = index_buff;
}

//插入排序
void Task_Insertion_Sort(Task_Queue *pq)
{
    Task_Node *head2;
    Task_Node *current;
    Task_Node *p;
    Task_Node *q;

    //第一次拆分
    head2 = pq->front->next;
    pq->front->next = NULL;

    while(head2 != NULL)
    {
        current = head2;
        head2 = head2->next; //控制循环结构体

        //寻找插入位置，找到第一个大于current数据元素的位置
        for(p=NULL, q=pq->front; q&&q->task.Qut_Queue_Delay <= current->task.Qut_Queue_Delay; p=q, q=q->next);

        if(q == pq->front)
        {
            //current 数据元素最大
            pq->front = current;

        }
        else
        {
            p->next = current;

        }
        current->next = q;
    }
}

//获得队列首元素
//自己添加的
int Task_GetFront_Queue(Task_Queue *pq,unsigned long *quit_time,uint32_t *freq_hz,uint8_t *bandwidth,uint32_t *datarate,uint8_t *coderate,uint16_t *size,uint8_t *payload,uint32_t *tx_count_us)
{
    Task_Node *p_buff;
    int index_buff;
    Task_Queue *queue_buff;
    queue_buff = (Task_Queue *)malloc(sizeof(Task_Queue));
    queue_buff = pq;

    //记录队列首地址
    p_buff = pq->front;
    //记录队列项目数
    index_buff = pq->items;

    if(Task_QueueIsEmpty(pq))
    {
        pq->front = NULL;
        return 1;
    }
    else
    {      
         *quit_time   =  queue_buff->front->task.Qut_Queue_Delay;
         *freq_hz     =  queue_buff->front->task.freq_hz;
         *bandwidth   =  queue_buff->front->task.bandwidth;
         *datarate    =  queue_buff->front->task.datarate;
         *coderate    =  queue_buff->front->task.coderate;
         *size        =  queue_buff->front->task.size;
         *tx_count_us =  queue_buff->front->task.tx_count_us;   
         memcpy(payload,queue_buff->front->task.payload,queue_buff->front->task.size);
    }
}
//==============================================================================================================//
