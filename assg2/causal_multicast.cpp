
/*
	g++ berkley.cpp -o berkley.o -lpthread
	./berkley.o 1

 include<cstdlib>
*/

#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <vector>
#include <sstream>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <queue>
#include <errno.h>

using namespace std;


struct message
{
	int msg_id;
	int pid;
	//vector<struct process_clock> v_clk;
	int vc[50];
};

struct process_group
{
	int pid;
	int port;
	int conn;
	char ip[16];
};

struct process_clock
{
	int pid;
	int clk;
};

struct process
{
	int pid;
	int port;
	char ip[16];
	vector<int> con_sock;
	int listen_sock;
	vector<struct process_clock> v_clk;
	vector<struct message> buffer;
	vector<struct process_group> p_group;
};

struct thread_data
{
	struct process *proc;
	//struct message msg;
	int conn;
};

/*
Create processes list to send multicast and retrieves its own port no. from file
Also initialize vector clocks to 0 for all processes.
*/

int fetch_port(vector<struct process_group> &p_group,vector<struct process_clock> &v_clk,int id)
{
	ifstream file;
	struct process_group p;
	string line,val1,val2;
	struct process_clock vc;
	char port[6];
	int self_port = -1;

	file.open("processConfig");

	while (getline(file,line)) 
	{	 
    	// read a line from file
		stringstream linestream(line);
		linestream >> val1 >> val2;
		strcpy(port,val1.c_str());
		p.pid = atoi(port);
		strcpy(port,val2.c_str());
		p.port = atoi(port);
		strcpy(p.ip,"127.0.0.1");
		p.conn = -1;
		p_group.push_back(p);
		vc.pid = p.pid;
		vc.clk = 0;
		v_clk.push_back(vc);
		if(id == p.pid)
		{	
			self_port = p.port;
			p_group.pop_back();
		}
	}

	file.close(); // close the file
	return self_port;
}

int get_index_of_vectorClk(vector<struct process_clock> v_clk, int pid)
{
	int i,size = v_clk.size();
	for(i=0;i<size;i++)
	{
		//cout<<i+1<<" P"<<v_clk[i].pid<<endl;
		if(v_clk[i].pid == pid)
			return i;
	}
	return -1;
}

int check_causality(struct process *proc, struct message msg)
{
	int i,index,size = proc->v_clk.size();
	int flg=1,spid = msg.pid;
	index = get_index_of_vectorClk(proc->v_clk,spid);
	if(msg.vc[index] != proc->v_clk[index].clk + 1)
	{
		return 0;
	}
	for(i=0;i<size;i++)
	{
		if(msg.vc[i] > proc->v_clk[i].clk && i!=index)
		{
			flg = 0;
		}
	}
	return flg;
}

void * receive_msg(void *arg)
{
	int i;
	struct thread_data *td = (struct thread_data *)arg;
	struct message msg;
	int byte_read,index;
	while(1)
	{
		byte_read = read(td->conn,&msg,sizeof(struct message));
		if(byte_read > 0)
		{
			cout<<"Recieved MsgId:"<<msg.msg_id<<" from P"<< msg.pid <<" Vector Clk:[";
			for(i = 0;msg.vc[i]!=-1 ;i++)
			{
				cout<<msg.vc[i]<<" ";
			}
			cout<<"]"<<endl;
			if(!check_causality(td->proc,msg))
			{
				//buffer_msg();
			}
			else
			{
				index = get_index_of_vectorClk(td->proc->v_clk,msg.pid);
				cout<<"Deliver msg_id:"<<msg.msg_id<<endl;
				td->proc->v_clk[index].clk = td->proc->v_clk[index].clk + 1;
			}
		}
	}
}

/*
struct message
{
	int msg_id;
	int pid;
	vector<struct process_clock> v_clk;
};
*/

void* reciever(void * arg)
{
	struct process *proc = (struct process *)arg;
	struct sockaddr_in server_addr,cli_addr;
	socklen_t clilen;
	struct message msg;
	pthread_t thread;
	struct thread_data td;
	int conn,byte_read,i;
	if((proc->listen_sock = socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
		printf("\nUnable to get server socket");
		pthread_exit(NULL);
	}
	
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(proc->port);

	if(bind(proc->listen_sock,(struct sockaddr *)&server_addr,sizeof(server_addr)))
	{
		printf("\nUnable to Bind");
		pthread_exit(NULL);
	}

	listen(proc->listen_sock,10);

	clilen = sizeof(cli_addr);
	cout<<"P"<<proc->pid<<" created Server Socket"<<endl;
	while(1)
	{
	
		//Accept Connections from Client
		//cout<<"Wating for others to send Connection Request"<<endl;
		conn = accept(proc->listen_sock,(struct sockaddr *)&cli_addr,&clilen);
		if (conn < 0) 
        {
			printf("\nClient accept request denied!!");
			continue;
		}
		//cout<<"connection estd"<<endl;
		//Prepare thread data and create New Thread
		//td.msg = msg;
		td.conn = conn;
		td.proc = proc;
		pthread_create(&thread,NULL,receive_msg,(void*)&td);
	}
}


void* sender(void * arg)
{
	struct process *proc = (struct process *)arg;
	struct message msg;
	int msg_id,i,byte_written,index;
	int proc_grp_size = proc->p_group.size();
	int vsize = proc->v_clk.size();	
	msg_id = 10 * proc->pid;
	//for()
	{
		msg.msg_id = msg_id;
		msg.pid = proc->pid;
		index = get_index_of_vectorClk(proc->v_clk, proc->pid);
		//cout<<"Index of P"<<proc->pid<<":"<<index<<endl;
		proc->v_clk[index].clk = proc->v_clk[index].clk + 1;
		for(i=0;i<vsize;i++)
		{
			msg.vc[i] = proc->v_clk[i].clk;
		}
		msg.vc[i] = -1;
		//msg.v_clk = proc->v_clk;
		for(i = 0; i<proc_grp_size ; i++)
		{
			sleep(proc->pid);
			cout<<"Send Msg from P"<< proc->pid<<" to P"<<proc->p_group[i].pid<<endl;
			byte_written = write(proc->p_group[i].conn, (void *)&msg, sizeof(struct message));
		}
	}
}

void prepare_q(queue<struct process_group> &connect_grp,vector<struct process_group> p_group)
{
	int i,size = p_group.size();
	for(i=0;i<size;i++)
	{
		//cout<<endl<<"Entering grpid in Q:"<<p_group[i].pid<<endl;
		connect_grp.push(p_group[i]);
	}
}

int get_index(vector<struct process_group> p_group, int pid)
{
	int i,size = p_group.size();
	for(i=0;i<size;i++)
	{
		if(p_group[i].pid == pid)
			return i;
	}
	return -1;
}
/*
	Establish connection with all other Processes.
*/
int create_connection(struct process &curr_proc)
{
	struct sockaddr_in serv_addr;
	struct hostent *server;
	int byte_read,byte_written;
	char server_ip[16];
	int server_port,cli_sock;
	queue<struct process_group> connect_grp;
	struct process_group proc;
	int index,i;

	prepare_q(connect_grp,curr_proc.p_group);

	while(!connect_grp.empty())
	{
		//Make Clients
		cli_sock = socket(AF_INET,SOCK_STREAM,0);
		if(cli_sock == -1)
		{
			printf("\nUnable to create Socket Descriptor %d",errno);
			return -1;
		}
		proc = connect_grp.front();
		connect_grp.pop();


		strcpy(server_ip,"127.0.0.1");
		server_port = proc.port;

		//Prepare Server Structure to Connect
		server = gethostbyname(server_ip);
		memset(&serv_addr,'0',sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(server_port);
		//inet_aton((char *)server->h_addr, /*(struct in_addr *)*/(char *)&serv_addr.sin_addr.s_addr);
		bcopy((char *)server->h_addr,(char *)&serv_addr.sin_addr.s_addr,server->h_length);

		//Connecct to Server
		if(connect(cli_sock,(struct sockaddr *)&serv_addr,sizeof(serv_addr))<0)
		{
			//cout<<"Unable to connect to P"<< proc.pid<<" Port = "<< proc.port <<endl;
			close(cli_sock);
			connect_grp.push(proc);
			//cout<<"Size of Queue:"<<connect_grp.size()<<endl;
			sleep(1);
			continue;
		}
		cout<<"Connected P"<<curr_proc.pid<<" with:P"<<proc.pid<<endl;
		index = get_index(curr_proc.p_group, proc.pid);
		if(index == -1)
		{
			cout<<"Error in finding process"<<endl;
		}
		curr_proc.p_group[index].conn = cli_sock;
	}
/*	for(i=0;i<curr_proc.p_group.size();i++)
	{
		if(curr_proc.p_group[i].conn != -1)
		{
			cout<<"P"<<curr_proc.pid<<"Connected to as client to P"<<curr_proc.p_group[i].pid<<endl;
		}
	}*/
	return 0;
}

int main(int argc, char *argv[])
{
	struct process self;
//	vector<struct process> process_list;
	vector<struct process_group>::iterator proc_it;
	vector<struct process_clock>::iterator vc_it;
	pthread_t listen_thread,send_thread;

	setbuf(stdout,NULL);
	self.pid = atoi(argv[1]);
	self.port=fetch_port(self.p_group, self.v_clk, self.pid);
	cout<<"P"<<self.pid<<" ,Port:"<<self.port<<endl;
	strcpy(self.ip,"127.0.0.1");

	if(self.port == -1)
	{
		cout<<"No id found"<<endl;
		return -1;
	}

	/*for(proc_it = self.p_group.begin(); proc_it!=self.p_group.end(); proc_it++)
	{
		cout<< proc_it->pid<<"*"<< proc_it->port <<endl;
	}*/
/*
	for(vc_it = self.v_clk.begin(); vc_it!=self.v_clk.end(); vc_it++)
	{
		cout<< vc_it->pid<<"*"<< vc_it->clk <<endl;
	}
*/
	//Create Recieving Thread
	pthread_create(&listen_thread,NULL,reciever,(void*)&self);
	create_connection(self);
/*
	create_connection() function ensures that all the connection are ready 
	and we can now send the Messages. Now create Sending Thread
*/
	pthread_create(&send_thread,NULL,sender,(void*)&self);
	//Wait for Reciever Thread.
	pthread_join(listen_thread,NULL);
	return 0;
}


