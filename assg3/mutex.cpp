
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

using namespace std;

#define REQ_CS 0
#define REL_CS 1
#define BYE_MSG 2

#define ACCESS_REQ 4

static int cs = 0;
pthread_cond_t cv;
pthread_mutex_t cslock;

struct request
{
	int pid;
	int msg_type;
};


struct process
{
	int pid;
	int port;
	char ip[16];
	vector<struct request> buffer;
	vector<struct request> connections;
	pthread_mutex_t qlock;
	int listen_sock;
};

struct thrd_data
{
	int csock;
	struct process *proc;
}thrd_data;


/*
This Function populates all the datastructure of the Struct Process
*/

int fetch_port(vector<struct process> &process_list,int id)
{
	ifstream file;
	struct process p;
	string line,val1,val2;
	char port[10];
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
		process_list.push_back(p);
		if(id == p.pid)
		{	
			self_port = p.port;
			process_list.pop_back();
		}
	}

	file.close(); // close the file
	return self_port;
}

/*
To get the index of the process with Pid = id
*/

int get_port(vector<struct process> process_list,int id)
{
	vector<struct process>::iterator proc_it;
	for(proc_it = process_list.begin(); proc_it!=process_list.end(); proc_it++)
	{
		if(proc_it->pid == id)
		{
			return proc_it->port;
		}
	}

	return -1;
}

/*
This funtion prints the contents of the buffer i.e which processes has requested the critical section.
*/

void print_buffer(struct process *proc)
{
	int i,size;
	size = proc->buffer.size();
	cout<<"Request Q:["<<proc->buffer[0].pid<<"]";
	for(i=1;i<size;i++)
	{
		cout<<"<<-["<<proc->buffer[i].pid<<"]";
	}
	cout<<endl;
}

int get_index(struct process *proc,int pid)
{
	int i,size;
	size = proc->connections.size();
	for(i = 0;i<size;i++)
	{
		if(proc->connections[i].pid == pid)
		{
			return i;
		}
	}
	return -1;
}

/*
This thread receives the requests from the other processes. If the request is to access the critical section, it buffers the request in the queue. If it is the Release of the CS then it notifies 'grant_permission' thread that the process has released the critical section. Finally if it is a BYE message it closes the connection with the process.
*/

void *update_buffer(void *arg)
{
	struct thrd_data *td = (struct thrd_data *)arg;
	int byte_read,pid,index;
	struct request req;
	while(1)
	{
		byte_read = read(td->csock, &req, sizeof(req));
		//cout<<"P"<<req.pid<<" MSG_TYPE:"<<req.msg_type<<endl;
		if(req.msg_type == REQ_CS)
		{
			cout<<"Request from P"<<req.pid<<endl;
			req.msg_type = td->csock;
			pthread_mutex_lock(&(td->proc->qlock));
			td->proc->buffer.push_back(req);
			print_buffer(td->proc);
			pthread_mutex_unlock(&(td->proc->qlock));
		}
		else if (req.msg_type == REL_CS)
		{
			pthread_mutex_lock(&cslock);
			if(cs == 1)
			{
				cs = 0;
				cout<<"CS Released by P"<<req.pid<<endl;
				pthread_cond_signal(&cv);
			}
			pthread_mutex_unlock(&cslock);
		}
		else if(req.msg_type == BYE_MSG)
		{
			cout<<"BYE from P"<<req.pid<<endl;
			break;
		}
	}
	index = get_index(td->proc,req.pid);
	td->proc->connections.erase(td->proc->connections.begin()+index);
	close(td->csock);
}

/*
This thread reads the requests from the queue and grants the access to the corresponding function. If someone else is using the critical section it then waits on the condition variable till it is signalled by the 'update_buffer' thread.
*/
void *grant_permission(void *arg)
{
	struct process *proc = (struct process *)arg;
	struct request req;
	int index,conn;
	//cout<<"Size of Connections:"<<proc->connections.size()<<endl;
	while(1)
	{
		if(proc->connections.size() == 0)
		{
			break;
		}
		pthread_mutex_lock(&cslock);
		if(cs == 1)
		{
			pthread_cond_wait(&cv,&cslock);
		}
		pthread_mutex_lock(&(proc->qlock));
		if(!proc->buffer.empty())
		{
			cs = 1;
			req = proc->buffer.front();
			proc->buffer.erase(proc->buffer.begin());
			index = get_index(proc,req.pid);
//			cout<<"Index:"<<index<<endl;
			conn = proc->connections[index].msg_type;
			if(index != -1)
			{
				write(conn, (void *)&(proc->pid), sizeof(int));
				cout<<"Access granted to P"<<req.pid<<endl;
			}
		}
		pthread_mutex_unlock(&(proc->qlock));
		pthread_mutex_unlock(&cslock);
	}

}

/*
This function makes itself the Centralized server.
*/
void make_self_server(struct process &self, vector<struct process> process_list)
{
	struct sockaddr_in server_addr,cli_addr;
	int byte_written, cli = 0,conn,pid,byte_read;
	socklen_t clilen;
	int no_of_process = process_list.size();
	struct request req;
	struct thrd_data td[no_of_process];
	pthread_t thrd[no_of_process];
	pthread_t permission;
	pthread_mutex_init(&cslock,NULL);
	pthread_mutex_init(&self.qlock,NULL);
	pthread_cond_init(&cv,NULL);
	setbuf(stdout,NULL);
	if((self.listen_sock = socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
		printf("\nUnable to get server socket");
		return;
	}

	//port = atoi(argv[2]);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(self.port);

	if(bind(self.listen_sock,(struct sockaddr *)&server_addr,sizeof(server_addr)))
	{
		printf("\nUnable to Bind");
		return;
	}

	listen(self.listen_sock,10);
	cout<<"*********Centalized Server*********"<<endl;
	clilen = sizeof(cli_addr);
	cli = 0;

	while(cli<no_of_process)
	{
	
		//Accept Connections from Client
		conn = accept(self.listen_sock,(struct sockaddr *)&cli_addr,&clilen);
		if (conn < 0) 
        {
			printf("\nClient accept request denied!!");
			continue;
		}

		byte_read = read(conn,&pid,sizeof(pid));
		cout<<"Connected to P:"<<pid<<endl;
		req.msg_type = conn;
		req.pid =  pid;
		self.connections.push_back(req);
		//cout<<"Pushed Connection I:"<<cli<<" P"<<pid<<endl;
		td[cli].csock = conn;
		td[cli].proc = &self;
		pthread_create(&thrd[cli],NULL,update_buffer,(void*)&td[cli]);
		cli++;
	}
		
	pthread_create(&permission,NULL,grant_permission,(void*)&self);

	
/*put join here to complete all threads then synchronize*/

	for(cli=0;cli<no_of_process;cli++)
	{
		pthread_join(thrd[cli],NULL);
	}

	close(self.listen_sock);
}

/*
Processes with PID not as 1 will be the processes that needs permission grant from the Centralized server to access the CS.
*/
void make_self_client(struct process &self, vector<struct process> process_list)
{
	struct sockaddr_in serv_addr;
	struct hostent *server;
	int i,byte_read,byte_written = -1;
	char server_ip[16];
	int server_port,grant,num;
	struct request req;
	FILE *file;
	setbuf(stdout,NULL);
	//Make Clients
	self.listen_sock = socket(AF_INET,SOCK_STREAM,0);
	if(self.listen_sock == -1)
	{
		printf("\nUnable to create Socket Descriptor");
	}
	strcpy(server_ip,"127.0.0.1");
	server_port = get_port(process_list,1);
	//Prepare Server Structure to Connect
	server = gethostbyname(server_ip);
	memset(&serv_addr,'0',sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(server_port);
	//inet_aton((char *)server->h_addr, /*(struct in_addr *)*/(char *)&serv_addr.sin_addr.s_addr);
	bcopy((char *)server->h_addr,(char *)&serv_addr.sin_addr.s_addr,server->h_length);
	//Connecct to Server
	if(connect(self.listen_sock,(struct sockaddr *)&serv_addr,sizeof(serv_addr))<0)
	{
		printf("\nError in Client Connection");
		return;
	}

	byte_written = write(self.listen_sock, (void *)&self.pid, sizeof(int));
	for(i=0;i<ACCESS_REQ;i++)
	{
		req.pid = self.pid;
		req.msg_type = REQ_CS;
		byte_written = write(self.listen_sock, (void *)&req, sizeof(req));

		byte_read = read(self.listen_sock, (void *)&grant, sizeof(int));
		cout<<"Acquired Lock of CS"<<endl;
		// open file
		file = fopen("test","r");
		fscanf(file,"%d",&num);
		fclose(file);
		cout<<"Content of file:"<<num<<endl;

		num++;
		file = fopen("test","w");
		cout<<"Writing the incremented counter back to file:"<<num<<endl;
		fprintf(file,"%d",num);
		fclose(file);
		req.pid = self.pid;
		req.msg_type = REL_CS;
		byte_written = write(self.listen_sock, (void *)&req, sizeof(req));
		
	}
	req.pid = self.pid;
	req.msg_type = BYE_MSG;
	byte_written = write(self.listen_sock, (void *)&req, sizeof(req));
	close(self.listen_sock);
}

int main(int argc, char *argv[])
{
	struct process self;
	vector<struct process> process_list;
	vector<struct process>::iterator proc_it;

	self.pid = atoi(argv[1]);
	self.port=fetch_port(process_list,self.pid);
	strcpy(self.ip,"127.0.0.1");

	if(self.port == -1)
	{
		cout<<"No id found"<<endl;
		return 0;
	}
/*
	for(proc_it = process_list.begin(); proc_it!=process_list.end(); proc_it++)
	{
		cout<< proc_it->pid<<"*"<< proc_it->port <<endl;
	}
*/

//Creating a centraized entity to allow exclusive access to file
	if(self.pid == 1)
		make_self_server(self,process_list);
	else
		make_self_client(self,process_list);


	return 0;
}


