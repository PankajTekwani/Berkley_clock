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

#define NO_OF_MULTICAST 4

static int start_causal = 0;
static int received = 0;
static int cs = 1;
static int start_mutex = 0;
pthread_mutex_t r_lock;
pthread_mutex_t cs_lock;

vector<int> ack;
pthread_mutex_t ack_lock;

struct request
{
	int pid;
	int req;
	int ack;
	int done;
};

struct message
{
	int msg_id;
	int pid;
	int is_causal;
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
	pthread_mutex_t buffer_lock;
	pthread_mutex_t vc_lock;
};

struct thread_data
{
	struct process *proc;
	//struct message msg;
	int conn;
};

/*
Reset the vector clocks
*/
void reset_vc(struct process *curr)
{
	int i;
	int size = curr->p_group.size() + 1;
	pthread_mutex_lock(&(curr->vc_lock));
	for(i=0;i<size;i++)
	{
		curr->v_clk[i].clk = 0;
	}
	pthread_mutex_unlock(&(curr->vc_lock));
	cout<<"Reset Vector Colcks"<<endl;
}


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

/*
Return the Index of Vector clk based on the Process ID 
*/
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

/*
Checks the Causality of the Multicast message
*/
int check_causality(struct process *proc, struct message msg)
{
	pthread_mutex_lock(&(proc->vc_lock));
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
	pthread_mutex_unlock(&(proc->vc_lock));
	return flg;
}

/*
This function delivers the message.
*/
void deliver_msg(struct process *proc, struct message msg)
{
	int i,size,index,excl;
	pthread_mutex_lock(&(proc->vc_lock));
	index = get_index_of_vectorClk(proc->v_clk,msg.pid);
	size = proc->v_clk.size();
	cout<<" Deliverd With Causality MsgId:"<<msg.msg_id<<endl;
	cout<<" Vector in Msg:[ ";
	for(i = 0; i<size ;i++)
	{
		cout<<msg.vc[i]<<" ";
	}
	cout<<"]";

	for(i = 0; i<size && i!= index;i++)
	{
		proc->v_clk[i].clk = max(proc->v_clk[i].clk,msg.vc[i]);
	}	

	proc->v_clk[index].clk = proc->v_clk[index].clk + 1;
	cout<<" P"<<proc->pid<<" Vector Clk:[";
	for(i = 0; i<size ;i++)
	{
		cout<<proc->v_clk[i].clk<<" ";
	}

	cout<<"]"<<endl;
	pthread_mutex_unlock(&(proc->vc_lock));
}

/*
If the message is not confirming the causality, this function is used to buffer the message.
*/

void push_into_buffer(struct process *proc, struct message msg)
{
	pthread_mutex_lock(&(proc->buffer_lock));
	proc->buffer.push_back(msg);
	pthread_mutex_unlock(&(proc->buffer_lock));
}


/*
To check the buffer if the causality is satisfied of the messages in the buffer.
*/
void check_buffer(struct process *proc)
{
	pthread_mutex_lock(&(proc->buffer_lock));
	int i;
	struct message msg;
	i=0;
	while(i<proc->buffer.size())
	{
		msg = proc->buffer[i];
		if(check_causality(proc,msg))
		{
			deliver_msg(proc,msg);
			proc->buffer.erase(proc->buffer.begin() + i);
			i=-1;
		}
		i++;
	}
	pthread_mutex_unlock(&(proc->buffer_lock));
}

void empty_buffer(struct process *proc)
{
	int i,size;
	struct message msg;
	//printf("Inside %s\n",__FUNCTION__);
	pthread_mutex_lock(&(proc->buffer_lock));
	while(!proc->buffer.empty())
	{
		msg = proc->buffer.back();
		proc->buffer.pop_back();
		if(check_causality(proc,msg))
		{
			deliver_msg(proc,msg);
		}
		else
		{
			proc->buffer.push_back(msg);
		}
	}
	pthread_mutex_unlock(&(proc->buffer_lock));
	//printf("Leaving %s\n",__FUNCTION__);
}

void * receive_msg(void *arg)
{
	struct thread_data *td = (struct thread_data *)arg;
	struct message msg;
	int byte_read,byte_written,i = 0,sl,j,conn,pid;
	int size = td->proc->v_clk.size();
	struct request packet;

// Recieve Non Causal Messages first.
	while(i<(NO_OF_MULTICAST))
	{
		byte_read = read(td->conn,&msg,sizeof(struct message));
		if(byte_read > 0)
		{
			cout<<"Recieved & Delivered Without Causality MsgId:"<<msg.msg_id<<" from P"<< msg.pid;
			cout<<"  , Vector Clk:[";
			for(j = 0; j<size ;j++)
			{
				cout<<td->proc->v_clk[j].clk<<" ";
			}

			cout<<"]"<<endl;

			//deliver_msg(td->proc,msg);
		}
		i++;
	}
	byte_written = write(td->conn, (void *)&j, sizeof(j));
	pthread_mutex_lock(&r_lock);
	received++;
	pthread_mutex_unlock(&r_lock);
	while(start_causal == 0)
	{
		sleep(1);
	}

//Recieve Causal Messages now.	
	i=0;
	while(i<(NO_OF_MULTICAST))
	{
		byte_read = read(td->conn,&msg,sizeof(struct message));
		srand (time(NULL));
		sl = rand() % 5;
		sleep(sl);
		if(byte_read > 0)
		{
			cout<<"Recieved MsgId:"<<msg.msg_id<<" from P"<< msg.pid;
			cout<<" with Vector Clk:[";
			for(j = 0; j<size ;j++)
			{
				cout<<msg.vc[j]<<" ";
			}

			cout<<"]"<<endl;

			if(msg.is_causal == 1 && check_causality(td->proc,msg))
			{
				deliver_msg(td->proc,msg);
				check_buffer(td->proc);
			}
			else
			{
				check_buffer(td->proc);
				if(check_causality(td->proc,msg))
				{
					deliver_msg(td->proc,msg);
					check_buffer(td->proc);
				}
				else
				{	//Push into Buffer;
					cout<<"Causality Violated, Buffering the MsgId:"<<msg.msg_id<<endl;
					push_into_buffer(td->proc, msg);
				}
			}
		}
		i++;
	}

	empty_buffer(td->proc);
	if(td->proc->buffer.size())
		cout<<"Buffer still not empty"<<endl;

	//close(td->conn);
}


void* reciever(void * arg)
{
	struct process *proc = (struct process *)arg;
	struct sockaddr_in server_addr,cli_addr;
	socklen_t clilen;
	struct message msg;
	int conn,byte_read,i,size;
	size = proc->p_group.size();
	pthread_t thread[size];
	struct thread_data td[size];
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
	//cout<<"P"<<proc->pid<<" created Server Socket"<<endl;

	i=0;
	size = proc->p_group.size();
	while(i<size)
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
		td[i].conn = conn;
		td[i].proc = proc;
		pthread_create(&thread[i],NULL,receive_msg,(void*)&td[i]);
		i++;
	}
	for(i=0;i<size;i++)
	{
		pthread_join(thread[i],NULL);
	}
}


void* sender(void * arg)
{
	struct process *proc = (struct process *)arg;
	struct message msg;
	int i,j,byte_written,index,x,sl,byte_read;
	int proc_grp_size = proc->p_group.size();
	int vsize,excl;	
	pthread_t mutex_thread;
	cout<<endl<<"*********Send Multicast without Causality***********"<<endl;
	for(x=0;x<NO_OF_MULTICAST;x++)
	{
		msg.msg_id = (100 * proc->pid) + x; //+i
		msg.pid = proc->pid;

		pthread_mutex_lock(&(proc->vc_lock));
		vsize = proc->v_clk.size();
		index = get_index_of_vectorClk(proc->v_clk, proc->pid);
		proc->v_clk[index].clk = proc->v_clk[index].clk + 1;

		for(i=0;i<vsize;i++)
		{
			msg.vc[i] = proc->v_clk[i].clk;
		}
		pthread_mutex_unlock(&(proc->vc_lock));
		msg.vc[i] = -1;
		//msg.v_clk = proc->v_clk;
		cout<<"Sending Multicast Msg id "<< msg.msg_id<<" [";
		for(j=0;j<vsize;j++)
		{
				cout<< msg.vc[j]<<" ";
		}
			cout<<"]"<<endl;

		// Send Multicast without Causality
		msg.is_causal = 0;
		for(i = 0; i<proc_grp_size ; i++)
		{
			byte_written = write(proc->p_group[i].conn, (void *)&msg, sizeof(struct message));		
		}
	}

//Waiting for Recievers Ack to ensure that recievers have recieved w/o Causal Multicasts
	for(i = 0; i<proc_grp_size ; i++)
	{
		byte_read = read(proc->p_group[i].conn, (void *)&x, sizeof(x));
	}
	
	while(received != proc_grp_size)
	{
		sleep(1);
	}
	reset_vc(proc);
	start_causal = 1;
	received = 0;
//Sending Causal Multicasts
	cout<<endl<<"*********Send Multicast with Causality***********"<<endl;
	for(x=0;x<NO_OF_MULTICAST;x++)
	{
		msg.msg_id = (100 * proc->pid) + x;
		msg.pid = proc->pid;

		pthread_mutex_lock(&(proc->vc_lock));
		vsize = proc->v_clk.size();
		index = get_index_of_vectorClk(proc->v_clk, proc->pid);
		proc->v_clk[index].clk = proc->v_clk[index].clk + 1;

		for(i=0;i<vsize;i++)
		{
			msg.vc[i] = proc->v_clk[i].clk;
		}
		pthread_mutex_unlock(&(proc->vc_lock));
		msg.vc[i] = -1;
		//msg.v_clk = proc->v_clk;
		cout<<"Sending Multicast Msg id "<< msg.msg_id<<" [";
		for(j=0;j<vsize;j++)
		{
				cout<< msg.vc[j]<<" ";
		}
			cout<<"]"<<endl;

		// Send Multicast with Causality
		msg.is_causal = 1;
		for(i = 0; i<proc_grp_size ; i++)
		{
			byte_written = write(proc->p_group[i].conn, (void *)&msg, sizeof(struct message));
			srand (time(NULL));
			sl = rand() % 3;
			sleep(sl);
			//cout<<"Sent msg id "<< msg.msg_id <<" from P"<< proc->pid<<" to P"<<proc->p_group[i].pid<<" [";		
		}
		srand (time(NULL));
		sl = rand() % 4;
		sleep(sl);
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
			//close(cli_sock);
			connect_grp.push(proc);
			//cout<<"Size of Queue:"<<connect_grp.size()<<endl;
			sleep(1);
			continue;
		}
		//cout<<"Connected P"<<curr_proc.pid<<" with:P"<<proc.pid<<endl;
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
	vector<struct process_group>::iterator proc_it;
	vector<struct process_clock>::iterator vc_it;
	pthread_t listen_thread,send_thread;

	setbuf(stdout,NULL);
	pthread_mutex_init(&self.buffer_lock,NULL);
	pthread_mutex_init(&self.vc_lock,NULL);
	pthread_mutex_init(&r_lock,NULL);
	self.pid = atoi(argv[1]);
	self.port=fetch_port(self.p_group, self.v_clk, self.pid);
	cout<<"P"<<self.pid<<" ,Port:"<<self.port<<endl;
	strcpy(self.ip,"127.0.0.1");

	if(self.port == -1)
	{
		cout<<"No id found"<<endl;
		return -1;
	}

	//Create Recieving Thread
	pthread_create(&listen_thread,NULL,reciever,(void*)&self);
	create_connection(self);
/*
	create_connection() function ensures that all the connection are ready 
	and we can now send the Messages. Now create Sending Thread
*/

	pthread_create(&send_thread,NULL,sender,(void*)&self);

//Wait for Reciever and Sender Thread.
	pthread_join(listen_thread,NULL);
	pthread_join(send_thread,NULL);
	

	return 0;
}


