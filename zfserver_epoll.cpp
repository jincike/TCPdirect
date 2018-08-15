#include <zf/zf.h>
#include "zf_utils.h"
#include <zf/muxer.h>

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>
#include <netinet/tcp.h>

#include <iostream>
#include <string>
#include <tuple>
#include <vector>
#include <sstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>

#include <signal.h>

using namespace std;

typedef unsigned long long u64_t;

#define BUFFER_SIZE 1024
#define MAX_EVENTS 10

#define HP_TIMING_NOW(Var) \
     ({ unsigned long long _hi, _lo; \
         asm volatile ("rdtsc" : "=a" (_lo), "=d" (_hi)); \
           (Var) = _hi << 32 | _lo; })

std::vector<std::tuple<int, u64_t, u64_t, int>> time_v;
static struct zf_muxer_set* muxer;

static void
ctrlc_handler(int s)
{
	u64_t sum = 0;
	int num = 0;
 	std::vector<std::tuple<int, u64_t, u64_t, int>>::iterator it;	

	for (it = time_v.begin(); it != time_v.end(); ++it) {
        cout << "len=" << std::get<0>(*it) << " --- recv time = " << std::get<1>(*it) << ", send time = " << std::get<2>(*it)
            << ", gap = " << std::get<3>(*it) << endl;
        if (it != time_v.begin()) {
            sum += std::get<3>(*it);
            ++num;
        }
    }
    std::cout << "avg gap = " << (sum / (num*1.0) ) << endl;
    std::cout << "avg gap(ns) = " << (sum / (num*3.4) ) << endl;

	exit(0);
}
int main(int argc, char **argv)
{
	cout<<"开始工作"<<endl;;
	struct sockaddr_in laddr;
    
	//信号触发，输出运行时间
	struct sigaction intrc_handle;
    intrc_handle.sa_handler = ctrlc_handler;
    sigemptyset(&intrc_handle.sa_mask);
    intrc_handle.sa_flags = 0;
    sigaction(SIGINT, &intrc_handle, NULL);
	
   //初始化tcpdirect和分配一个栈
	 ZF_TRY(zf_init());
	 
	 struct zf_attr* attr;
     ZF_TRY(zf_attr_alloc(&attr));

     struct zf_stack* stack;
	 ZF_TRY(zf_stack_alloc(attr, &stack));

	 cout<<"初始化完成"<<endl;;
	 
	 //绑定本地 地址和端口
	 memset(&laddr,0,sizeof(laddr));
     laddr.sin_family=AF_INET;
     laddr.sin_addr.s_addr=inet_addr("192.168.1.99");
     laddr.sin_port=htons(9002);
	 
	 //定义监听zocket和成功连接zocket
	 struct zftl* listener;
	 struct zft* zock;
	 //绑定本地端口地址，返回监听套接字
	 ZF_TRY(zftl_listen(stack, (sockaddr*)&laddr, sizeof(struct sockaddr), attr, &listener));
	
	 cout<<"监听建立"<<endl;
	 struct epoll_event event;
	 event.events = EPOLLIN;
	 event.data.ptr =&listener;
	 //初始化muxer,用来实现epoll功能
     ZF_TRY(zf_muxer_alloc(stack, &muxer));
     ZF_TRY(zf_muxer_add(muxer, zftl_to_waitable(listener), &event));
	 cout<<"epoll成功创建"<<endl;
	 
	 int nfds;
	 u64_t t0, t1;
	 while (1) {
		struct epoll_event evs[MAX_EVENTS];
		
		cout<<"等待连接"<<endl;
		nfds=zf_muxer_wait(muxer, evs, MAX_EVENTS,-1);
		if(nfds<0) {
			ostringstream oss;
        	oss << "start epoll_wait failed";
        	throw runtime_error(oss.str());
		}
		cout<<nfds<<endl;
        int first_recv = 1;
		int i;
		cout<<"成功"<<endl;
		
		char buf[BUFFER_SIZE];
		struct iovec siov = { buf,strlen(buf)};
		
		struct rd{ 
		struct zft_msg zcr;
		struct iovec iov[1]; 
		} rd1;
		
		rd1.zcr.iovcnt = 1;
		int client_sockf;
		ssize_t len3;
		for(i=0;i<nfds;i++) {
			if(client_sockf=zftl_accept(listener,&zock)==0) {
				cout<<"建立成功"<<endl;
				}
			 //Adds a waitable object to a multiplexer set.
			ZF_TRY(zf_muxer_add(muxer, zft_to_waitable(zock), &evs[i]));
			
			 //初始化事件和硬件。 processes events on a stack and performs the necessary handling.
             //These events include transmit and receive events raised by the hardware, 
			 //and also software events such as TCP timers. Applications must call
             //this function or zf_muxer_wait() frequently for each stack that is in use. 
			zf_reactor_perform(stack);
			rd1.zcr.iovcnt = 1;
			HP_TIMING_NOW(t0);
			 //使用的零拷贝recv，zft_recv()基于拷贝的总是无法收到数据。没有返回值
			zft_zc_recv(zock, &rd1.zcr, 0);
			if( rd1.zcr.iovcnt == 0 )
					continue;
			if( first_recv ) {
				first_recv = 0;
				siov.iov_len = rd1.zcr.iov[0].iov_len;
				//两种数据拷贝
				memcpy(buf, ((char*)rd1.zcr.iov[0].iov_base), siov.iov_len);
				}			
			for( int i = 0 ; i < rd1.zcr.iovcnt; ++i ) {
				len3=zft_send(zock, &siov, 1, 0);
				}
			HP_TIMING_NOW(t1);
				
			time_v.push_back(std::make_tuple(len3,t1, t0, (t1 - t0)));
			cout<<"服务器发送："<<len3<<"数据："<<buf<<endl;
			
			zft_zc_recv_done(zock, &rd1.zcr);
		}
		zf_muxer_free(muxer);
	}
	ZF_TRY(zft_shutdown_tx(zock));
	ZF_TRY(zft_free(zock));
	sleep(600);
	return 0;	
}
