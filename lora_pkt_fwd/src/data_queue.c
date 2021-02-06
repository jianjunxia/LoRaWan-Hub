#include "data_queue.h"
#include  <string.h>
//==================================队列函数实现===================================//
//局部函数
//把项拷贝到节点中
//初始化队列
static void CopyToNode(Data data, Data_Node *pn)
{
    pn->data.size        = data.size;   
    memcpy(pn->data.payload,data.payload,data.size);
}

//局部函数，把节点拷贝到项中
static void CopyToItem(Data_Node *pn, Data *pi)
{
    *pi = pn->data;
}

void Data_InitializeQueue(Data_Queue *pq)
{
    pq->front = pq->rear = NULL;
    pq->items = 0;
}
// 检查队列是否已满        
bool Data_QueueIsFull(const Data_Queue *pq)
{
    return pq->items == MAX_DATA_QUEUE; //检查队列是否已满
}
//  检查队列是否为空        
bool Data_QueueIsEmpty(const Data_Queue *pq)
{
    return pq->items == 0; //检查队列项数是否为0    
}

//  返回队列中的项数        
int Data_QueueItemCount(const Data_Queue *pq)
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
bool Data_EnQueue(Data data, Data_Queue *pq)
{
    Data_Node *pnew;

    //检查队列是否已满
    if(Data_QueueIsFull(pq))
    {
        return false;
    }
    //给新节点分配空间
    pnew = (Data_Node*)malloc(sizeof(Data_Node));  
    if(pnew == NULL)
    {
        fprintf(stderr,"Unable to allocate memory!\n");
    }
    CopyToNode(data,pnew);  //把项拷贝到节点中
    pnew->next = NULL;      //设置当前节点的next为null,表明这是最后一个节点
    if(Data_QueueIsEmpty(pq))    //如果该节点是第一个入队的节点，则该节点位于队列的首端
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
bool Data_DeQueue(Data *ptask, Data_Queue *pq)
{
    Data_Node *pt;

    if(Data_QueueIsEmpty(pq))
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
void Data_EmptyTheQueue(Data_Queue *pq)
{
    Data    dummy;
    while(!Data_QueueIsEmpty(pq))
    {
        Data_DeQueue(&dummy,pq);

    }
}

//列出队列中所有的节点
//自己添加的
void Data_List_Queue(Data_Queue *pq)
{
    Data_Node *p_buff;
    int index_buff;

    //记录队列首地址
    p_buff = pq->front;
    //记录队列项目数
    index_buff = pq->items;

    DEBUG_CONF(" list queue: %d\n",pq->items);
    if(Data_QueueIsEmpty(pq))
    {
        DEBUG_CONF("queue is not exit\n");
    }
    else
    {
         DEBUG_CONF("queue: \n");
         while(pq->items != 0)
         {
            DEBUG_CONF("data->size:       %d\n",pq->front->data.size);
            for(int i=0; i< pq->front->data.size;i++)
            {
                DEBUG_CONF("data[%d]:0x%x\n",i,pq->front->data.payload[i]);
            }
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
//取服务器下发给节点的数据
//parameter: 队列地址，取出的数据大小，取出的数据

int Data_GetFront_Queue(Data_Queue *pq,uint16_t *size,uint8_t *data)
{
    Data_Node *p_buff;
    int index_buff;
    Data_Queue *queue_buff;
    queue_buff = (Data_Queue *)malloc(sizeof(Data_Queue));
    queue_buff = pq;

    //记录队列首地址
    p_buff = pq->front;
    //记录队列项目数
    index_buff = pq->items;
    DEBUG_CONF("fetch server ack data...\n"); 
    if(Data_QueueIsEmpty(pq))
    {
        pq->front = NULL;
        return 1;
    }
    else
    {
        *size =   pq->front->data.size;
        mymemcpy(data,pq->front->data.payload,pq->front->data.size);

    }
}


