
#include "delay_queue.h"

//==================================队列函数实现===================================//
/**
 * 局部函数
 * 把项拷贝到节点中
 */
static void CopyToNode(Item item, Node *pn)
{
    pn->item.delay           = item.delay;
    pn->item.Qut_Queue_Delay = item.Qut_Queue_Delay;
    pn->item.T1              = item.T1;
    pn->item.devnonce        = item.devnonce;
    pn->item.count_us        = item.count_us;
    pn->item.real_delay      = item.real_delay;
    pn->item.tx_count_us     = item.tx_count_us; 
    pn->item.freq_hz         = item.freq_hz; 
    pn->item.coderate        = item.coderate; 
    pn->item.datarate        = item.datarate; 

    memcpy(pn->item.deveui,item.deveui,8);
    memcpy(pn->item.appeui,item.appeui,8);
}

/**
 * 局部函数
 * 把节点拷贝到项中
 * 
 */
static void CopyToItem(Node *pn, Item *pi)
{
    *pi = pn->item;
}

/**
 * 把指向队列首项和尾项的指针设置为NULL
 * 把项数设置为0；
 * 
 */ 
void InitializeQueue(Queue *pq)
{
    pq->front = pq->rear = NULL;
    pq->items = 0;
}

/* 检查队列是否为满 */
bool QueueIsFull(const Queue *pq)
{
    return pq->items == MAXQUEUE;
}

/* 检查队列是否为空 */
bool QueueIsEmpty(const Queue *pq)
{

    return pq->items == 0;
}

/* 返回队列的项数 */
int QueueItemCount(const Queue *pq)
{
    return pq->items;    
}

/**
 * 把项添加到队列中，包括以下几个步骤：
 * 1:创建一个新的节点
 * 2:把项拷贝到节点中
 * 3:设置节点的next指针为NULL，表明该节点是最后一个节点；
 * 4:设置当前尾节点的next指针指向新节点，把新节点链接到队列中；
 * 5:把rear指针指向新节点，以便找到最后的节点；
 * 6：项目 +1
 */
bool EnQueue(Item item, Queue *pq)
{
    Node *pnew;

    /* 检查队列是否已经满了 */
    if ( QueueIsFull(pq) ){

            return false;
    }

    /* 为节点分配内存 */
    pnew = (Node*)malloc(sizeof(Node));
    if ( pnew == NULL ){

            fprintf(stderr,"Unable to allocate memory!\n");
    }
    
    /* 把项拷贝到新创建的节点中去 */
    CopyToNode(item,pnew);  

    /* 设置当前节点的next为null，表明该节点是最后一个节点 */
    pnew->next = NULL;     
    
    /* 如果该节点是第一个入队的节点,则该节点位于队列首端 */   
    if ( QueueIsEmpty(pq) ) {
            
            pq->front = pnew;

    }else {
            
            pq->rear->next = pnew; /* 链接到队列尾端 */
    }

    pq->rear = pnew;  /* 记录队列尾端的位置 */
    pq->items++;
    
    return true;
}

/**
 * 从队列的首端删除项，涉及以下几个步骤
 * 1:把项拷贝到给定的变量中；
 * 2:释放空出的节点使用的内存空间；
 * 3:重置首指针指向队列中的下一个项；
 * 4:如果删除最后一项，把首指针和尾指针都重置为NULL；
 * 5:项数-1；
 */
bool DeQueue(Item *pitem, Queue *pq)
{
    Node * pt;

    if(QueueIsEmpty(pq)){

        return false;
    }

    CopyToItem(pq->front,pitem);

    pt = pq->front;
    
    /* 节点前移 */
    pq->front = pq->front->next;

    /* 释放空节点的内存空间 */
    free(pt);    
    
    pq->items--; //项目数-1
    
    if(pq->items == 0 ){

        pq->rear = NULL;
    }
   
    return true;
}

//清空队列
void EmptyTheQueue(Queue * pq)
{
    Item dummy;
    while(!QueueIsEmpty(pq))
    {
        DeQueue(&dummy,pq);

    }
}

//列出队列中所有的数据
void List_Queue(Queue *pq)
{
    
    Node *p_buff;
    int  index_buff;

    //记录队列首地址
    p_buff     = pq->front;
    //记录队列项目数
    index_buff = pq->items;

    DEBUG_CONF("list queue:%d\n",pq->items);   
    if(QueueIsEmpty(pq))
    {
        printf("不存在队列\n");
        pq->front = NULL;
    }
    else
    {
        DEBUG_CONF("queue: \n");
        while(pq->items != 0)
        {
                     
            DEBUG_CONF("node->T1:       %lu\n",pq->front->item.T1);
            DEBUG_CONF("node->delay:    %d\n" ,pq->front->item.delay);
            DEBUG_CONF("node->out_time: %lu\n",pq->front->item.Qut_Queue_Delay);
            DEBUG_CONF("node->deveui:   %x"   ,pq->front->item.deveui[0]);
            printf(" %x"                  ,pq->front->item.deveui[1]);
            printf(" %x"                  ,pq->front->item.deveui[2]);
            printf(" %x"                  ,pq->front->item.deveui[3]);
            printf(" %x"                  ,pq->front->item.deveui[4]);
            printf(" %x"                  ,pq->front->item.deveui[5]);
            printf(" %x"                  ,pq->front->item.deveui[6]);
            printf(" %x\n"                ,pq->front->item.deveui[7]);
            printf(" %x"                  ,pq->front->item.appeui[1]);
            printf(" %x"                  ,pq->front->item.appeui[2]);
            printf(" %x"                  ,pq->front->item.appeui[3]);
            printf(" %x"                  ,pq->front->item.appeui[4]);
            printf(" %x"                  ,pq->front->item.appeui[5]);
            printf(" %x"                  ,pq->front->item.appeui[6]);
            printf(" %x\n"                ,pq->front->item.appeui[7]);

            DEBUG_CONF("node->devnonce: %x\n" ,pq->front->item.devnonce);
            pq->items--; 
            pq->front = pq->front->next;
            DEBUG_CONF("pq->item:%d\n",pq->items);  
  
        }
        DEBUG_CONF("end\n");
    } 
    //恢复首地址
    pq->front = p_buff;
    pq->items = index_buff;
}

//将队列中的数据进行排序
void Bubble_Sort(Queue *pq)
{
    int i;
    int j;
    //int  tmp;
    //Node 
    int item_buff;
    Item swap;
   
    Node *p;
    Node *p_move;
  
    memset(&swap,0,sizeof(Item));

    //存储首地址
    p      = pq->front;
    p_move = pq->front;

    item_buff = pq->items;

    DEBUG_CONF("pq-->items:%d\n",pq->items);
    
    if(QueueIsEmpty(pq))
    {
        DEBUG_CONF("not exit this queue\n");
        pq->front = NULL;
    }
    else
    {
        
        while( pq->items > 1 )
        {
            while(pq->front->next !=NULL)
            {
                if(pq->front->item.Qut_Queue_Delay > pq->front->next->item.Qut_Queue_Delay)
                {
                    
                    //printf("pq->front->item: %d\n",pq->front->item);
                    //printf("pq->items: %d\n",pq->items);
                    //tmp = pq->front->item;
                    
                    //节点交换,逐个交换结构体成员                   
                    swap.delay           =  pq->front->item.delay;
                    swap.Qut_Queue_Delay =  pq->front->item.Qut_Queue_Delay;
                    swap.T1              =  pq->front->item.T1;
                    swap.devnonce        =  pq->front->item.devnonce;
                    memcpy(swap.deveui,pq->front->item.deveui,8);
                    memcpy(swap.appeui,pq->front->item.appeui,8);
                                      
                    pq->front->item.delay           = pq->front->next->item.delay;
                    pq->front->item.Qut_Queue_Delay = pq->front->next->item.Qut_Queue_Delay;
                    pq->front->item.T1              = pq->front->next->item.T1;
                    pq->front->item.devnonce        = pq->front->next->item.devnonce;  
                    memcpy(pq->front->item.deveui,pq->front->next->item.deveui,8);
                    memcpy(pq->front->item.appeui,pq->front->next->item.appeui,8);                        
                   
                    pq->front->next->item.delay           = swap.delay;
                    pq->front->next->item.Qut_Queue_Delay = swap.Qut_Queue_Delay;
                    pq->front->next->item.T1              = swap.T1;
                    pq->front->next->item.devnonce        = swap.devnonce;
                    memcpy(pq->front->next->item.deveui,swap.deveui,8);
                    memcpy(pq->front->next->item.appeui,swap.appeui,8);    
                }
                pq->front = pq->front->next;
            }

            pq->items--;
            //重新移动到第一个节点
            pq->front = p_move;
        }        

        //恢复初始地址
        pq->front = p;
        pq->items = item_buff;
        List_Queue(pq);
    }

}

/* 插入排序 */
void Insertion_Sort(Queue *pq)
{   
    Node *head2;
    Node *current;
    Node *p;
    Node *q;

    /* 第一次拆分 */
    head2 = pq->front->next;
    pq->front->next = NULL;

    while(head2 != NULL)
    {
        current = head2;
        /* 控制循环结构体 */
        head2 = head2->next;

        /* 寻找插入位置，找到第一个大于current数据元素的位置 */
        for(p=NULL, q=pq->front; q&&q->item.Qut_Queue_Delay <= current->item.Qut_Queue_Delay; p=q, q=q->next );

        if(q == pq->front){

                /* current 数据元素最大 */
                pq->front = current;

        } else{

                p->next = current;

        }
        current->next = q;
    }
    
}


/**
 * 获取队列首元素
 * 输入：队列名
 * 输出：quit_time
 * 输出：deveui
 * 输出：dev_nonce
 * 输出：appeui 
 * 输出：tx_count_us
 * 输出：freq_hz
 * 输出：coderate
 * 输出：datarate
 */
int GetFront_Queue(Queue *pq,unsigned long *quit_time,uint8_t  *deveui,
                                                      uint16_t *dev_nonce,
                                                      uint8_t  *appeui,
                                                      uint32_t *tx_count_us,
                                                      uint32_t *freq_hz,
                                                      uint8_t  *coderate,
                                                      uint32_t *datarate)
{
    
    Node *p_buff;
    int  index_buff;
    Queue *queue_buff;
    queue_buff = (Queue *)malloc(sizeof(Queue));
    queue_buff = pq;
    
    //记录队列首地址
    p_buff     = pq->front;
    //记录队列项目数
    index_buff = pq->items;
    
    if(QueueIsEmpty(pq))
    {
            pq->front = NULL;
            return 1;
            
    } else{
            *quit_time    =  (unsigned long)queue_buff->front->item.Qut_Queue_Delay;
            *dev_nonce    =  queue_buff->front->item.devnonce;
            *tx_count_us  =  queue_buff->front->item.tx_count_us;
            *freq_hz      =  queue_buff->front->item.freq_hz;  
            *coderate     =  queue_buff->front->item.coderate;  
            *datarate     =  queue_buff->front->item.datarate;

            memcpy(deveui,queue_buff->front->item.deveui,8);
            memcpy(appeui,queue_buff->front->item.appeui,8);
    } 
}