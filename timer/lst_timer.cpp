#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)  //增加一个新的定时器在双向链表上
{
    if (!timer)  //如果新增的节点为空，直接返回
    {
        return;
    }
    if (!head)  //如果当前链表为空，则新增的这个定时器成为首个节点
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) //有可能新增的节点的过期时间小于头结点的过期时间，这可能是由于头节点以及后面的节点一直在刷新时间
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}
void sort_timer_lst::adjust_timer(util_timer *timer)  //因读写事件增加过期时间，某节点的定时器需要向后调整位置
{
    if (!timer)  //如果定时器节点为空直接返回
    {
        return;
    }
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))   //如果该定时器节点的下一个节点为空则表示这是尾节点，不用调整。 或者该节点增加的时间小于下一个节点，则不用调整。
    {
        return;
    }
    if (timer == head)   //如果要调整的节点是头结点，则先删除这个节点，然后通过add_timer函数插入
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        // 如果要调整的节点不是头结点，则先删除这个节点，然后通过add_timer函数插入
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) // 增加一个新的定时器在双向链表上
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp)  //循环直到找到前一个小于插入节点的过期时间
    {
        if (timer->expire < tmp->expire)  
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp)  //如果当前tmp==nullptr,则pre为尾节点，新增节点timer放在其后面
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

void sort_timer_lst::del_timer(util_timer *timer)   //删除某一个定时器节点
{
    if (!timer)
    {
        return;
    }
    if ((timer == head) && (timer == tail))  //只有一个节点时
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head)   //不止一个节点，且要删除的节点在头部
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail) // 不止一个节点，且要删除的节点在尾部
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }                 // 不止一个节点，且要删除的节点在中间
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::tick()  //寻找所有超时的定时器并删除
{
    if (!head)
    {
        return;
    }
    
    time_t cur = time(NULL);
    util_timer *tmp = head;
    while (tmp)
    {
        if (cur < tmp->expire)
        {
            break;
        }
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}



void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);        // 这一行调用了 fcntl 函数，并传入了文件描述符 fd 和 F_GETFL 常量。F_GETFL 用于获取文件描述符的当前标志。old_option 变量存储了当前的标志位
    int new_option = old_option | O_NONBLOCK;   // 这一行创建了一个新的变量 new_option，它是 old_option 与 O_NONBLOCK 标志位的按位或运算结果
    fcntl(fd, F_SETFL, new_option);             // 这一行再次调用了 fcntl 函数，这次使用了 F_SETFL 常量，它用于设置文件描述符的标志.new_option 被作为新的标志传递给 fcntl，使文件描述符变为非阻塞
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();   //删除超时的定时器
    alarm(m_TIMESLOT);    //重新设置定时器
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
