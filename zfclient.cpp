#include <zf/zf.h>
#include "zf_utils.h"

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


using namespace std;

typedef unsigned long long u64_t;

#define BUFFER_SIZE 2048

#define HP_TIMING_NOW(Var) \
     ({ unsigned long long _hi, _lo; \
         asm volatile ("rdtsc" : "=a" (_lo), "=d" (_hi)); \
           (Var) = _hi << 32 | _lo; })

char test512[] = {"data12345passwd12345me f-stack+dpdk test_in_intelx710datatestfordpdkmngfdff_freebsd_cfgstruct4532990123"
                "12345stockidnameTNL_D_OPENTNL_OPT_LIMIT_PRICE TNL_HF_ARBITRAGE TNL_OS_UNREPORT0123onlyonesadaschg56432120kjkdsad"
                "longPos100longYd18longTd45longPosFrozen10longYdFrozen5longTdFrozen3STATUS_UNKNOWNTNL_HF_SPECULATIONdfdaz8754399"
                "kunshanTNL_D_SELL0TNL_D_CLOSETODAY2TNL_HF_HEDGE1TNL_OS_PARTIALCOMpTNL_OS_WITHDRAWINGf promiscuous"
                "helloworldcontractDict0234hgjdfv0x1000jfgksadasEVENT_CONTRACTSTATUS_PARTTRADEDproc_lcore"
};


int main(int argc, char **argv)
{
	int i=0,len=0;
	
	struct sockaddr_in laddr;
	struct sockaddr_in raddr;
	//绑定远程 地址和端口
	memset(&raddr,0,sizeof(raddr));
    raddr.sin_family=AF_INET;
    raddr.sin_addr={inet_addr("192.168.1.99")};
    raddr.sin_port=htons(9002);
	//绑定本地 地址和端口
	memset(&laddr,0,sizeof(laddr));
    laddr.sin_family=AF_INET;
	laddr.sin_addr={inet_addr("192.168.1.101")};
    laddr.sin_port=htons(9010);
	
	
	 //初始化tcpdirect和分配一个栈
	 ZF_TRY(zf_init());
	 
	 struct zf_attr* attr;
     ZF_TRY(zf_attr_alloc(&attr));

     struct zf_stack* stack;
	 ZF_TRY(zf_stack_alloc(attr, &stack));
     
	 struct zft_handle* tcp_handle;
	 ZF_TRY(zft_alloc(stack, attr, &tcp_handle));
	 cout<<"初始化完成"<<endl;
	 
	 //tcp_handle是本地未连接zocket,用来绑定本地地址
	 if(zft_addr_bind(tcp_handle, (sockaddr*)&laddr,sizeof(struct sockaddr),0)==0){
		 cout<<"bind成功"<<endl;
	 }
	 //tcp_handle与远程地址成功连接  会返回远程连接zocket---zft* tcp
	 struct zft* tcp;
     if(zft_connect(tcp_handle, (sockaddr*)&raddr,sizeof(struct sockaddr),&tcp)==0){
		 cout<<"连接成功"<<endl;
	 }
	//对栈的状态初始化（软硬件，文档上这么写的）
	cout << "state: " << zft_state(tcp) << endl;
	while( zft_state(tcp) == TCP_SYN_SENT )
    	zf_reactor_perform(stack);
	cout << "state: " << zft_state(tcp) << endl; 

	struct {
		struct zft_msg zcr;
		struct iovec iov[1];
		}rd;
	 u64_t t0, t1;
	 size_t send_size=1;
	 //内存起始地址，这块内存长度
	 struct iovec siov = { test512, strlen(test512)};
	 
	 struct rd{ 
     struct zft_msg msg;
     struct iovec iov[1]; 
     } rd1;
	 rd1.msg.iovcnt = 1;
	 cout<<rd1.msg.iov[0].iov_len<<endl;
	 cout<<sizeof(test512)<<endl;
	 
	 std::vector<std::tuple<int, u64_t, u64_t, int>> time_v;
	 time_v.reserve(1000);
	
	for (i = 0; (i < 1000) && (send_size>0); ++i) {
		
		HP_TIMING_NOW(t0);
		//查询栈空间大小，send_size传出大小
		ZF_TRY(zft_send_space(tcp,&send_size));
	
	    ssize_t lens=zft_send(tcp, &siov,1 , 0);
		cout<<lens<<endl;
			
		//void *p=NULL;
		//必须加的,对运行栈的初始化工作（events初始化和硬件初始化，文档里写的）
		zf_reactor_perform(stack);
		rd1.msg.iovcnt = 1;
		zft_zc_recv(tcp, &rd1.msg, 0);
			
		cout<<"接收到了数据"<<rd1.msg.iovcnt<<endl;
		/*	
		for (i = 0; (i <rd1.msg.iovcnt) && (i<6);++i ){    
			p=rd1.msg.iov[i].iov_base;
			cout<<"收到数据 " <<rd1.msg.iov[i].iov_base<<"数值："<<*((int*)p)<<endl;
				
		}
		*/	
		HP_TIMING_NOW(t1);
		time_v.push_back(std::make_tuple(lens,t1, t0, (t1 - t0)));
	   
        }
	
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
    std::cout << "avg gap(ns) = " << (sum / (num*3.4)) << endl;
	//sleep(600);
	ZF_TRY(zft_shutdown_tx(tcp));
    ZF_TRY(zft_free(tcp));
	
	return 0;
}
