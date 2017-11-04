
/*
	g++ berkley.cpp -o berkley.o -lpthread
	./berkley.o 1

 include<cstdlib>
*/

#include<iostream>
#include<fstream>
#include<stdlib.h>
#include<vector>
#include<sstream>
#include<string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>

using namespace std;


struct message
{
	int offset;
	int pid;
	int conn;
};


struct process
{
	int pid;
	int port;
	char ip[16];
	int clock;
	vector<int> con_sock;
	vector<struct message> msg_vector;
	int listen_sock;
};

struct thrd_data
{
	int csock;
	struct process *proc;
}thrd_data;

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

void *update_clk_vector(void *arg)
{
	struct thrd_data *td = (struct thrd_data *)arg;
	int byte_read;
	struct message msg;
	byte_read = read(td->csock, &msg, sizeof(struct message));
	cout<<"Offset at Client ["<<msg.pid<<"] :"<<msg.offset<<endl;
	//cout<<"Sizeof(message):"<<sizeof(struct message)<<" byteread:"<<byte_read<<endl;
	//cout<<"ConnId at Client ["<<msg.pid<<"] :"<<msg.conn<<endl;
	msg.conn = td->csock;
	td->proc->msg_vector.push_back(msg);
	
}

int sync_clocks(vector<struct message> &msg_vector)
{
	vector<struct message>::iterator it;
	int sum = 0,avg,no_of_cli;
	no_of_cli = msg_vector.size();
	for(it=msg_vector.begin();it!=msg_vector.end();it++)
	{
		sum+=it->offset;
	}
	avg = sum / (no_of_cli + 1);
	//cout<<"Sum:"<<sum<<" Avg:"<<avg<<endl;
	for(it=msg_vector.begin();it!=msg_vector.end();it++)
	{
		it->offset = avg - it->offset;
	}
	cout<<"Avg of all Offsets:"<<avg<<endl;
	return avg;
}

void send_offsets(vector<struct message> msg_vector)
{
	vector<struct message>::iterator it;
	int byte_written,i,size;
	struct message msg;
	for(it=msg_vector.begin();it!=msg_vector.end();it++)
	{
		//cout<<"Send["<<it->pid<<"] offset:"<<it->offset<<endl;
		msg.offset = it->offset;
		byte_written = write(it->conn, (void *)&msg, sizeof(struct message));
		close(it->conn);
	}
	size = msg_vector.size();
	for(i =0 ;i<size;i++)
	{
		msg_vector.pop_back();
	}
	
}

void make_self_server(struct process &self, vector<struct process> process_list)
{
	struct sockaddr_in server_addr,cli_addr;
	int byte_written, cli = 0;
	int offset;
	int conn;
	socklen_t clilen;
	int no_of_clients = process_list.size();
	struct thrd_data td[no_of_clients];
	pthread_t thrd[no_of_clients];
	struct message msg;
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
	cout<<"*********Time Daemon*********"<<endl;
	cout<<"Clock at Time Daemon:"<<self.clock<<endl;
	clilen = sizeof(cli_addr);
	cli = 0;
	//cout<<"no_of_clients = "<<no_of_clients<<endl;
	//offset.push_back(self.clock);
	cout<<"Sending Clock to all Clients."<<endl;
	while(cli<no_of_clients)
	{
	
	//Accept Connections from Client
		conn = accept(self.listen_sock,(struct sockaddr *)&cli_addr,&clilen);
		if (conn < 0) 
        {
			printf("\nClient accept request denied!!");
			continue;
		}
		msg.offset = self.clock;
		msg.pid = self.pid;
		byte_written = write(conn, (void *)&msg, sizeof(struct message));
		//Prepare thread data and create New Thread
		
		td[cli].csock = conn;
		td[cli].proc = &self;
		pthread_create(&thrd[cli],NULL,update_clk_vector,(void*)&td[cli]);
		cli++;
	}
/*put join here to complete all threads then synchronize*/

	for(cli=0;cli<no_of_clients;cli++)
	{
		pthread_join(thrd[cli],NULL);
		//cout<<"Waiting for client:"<<(cli + 2)<<endl;
	}

	cout<<"*****Sync Start*****"<<endl<<endl;
/*	for(cli=0;cli<no_of_clients;cli++)
	{
		cout<<"Pid:"<< self.msg_vector[cli].pid<<"->Offset:"<<self.msg_vector[cli].offset<<endl;
	}
*/
	offset = sync_clocks(self.msg_vector);
	self.clock += offset;
	cout<<"Synced Clock at Time Deamon:"<<self.clock<<endl;
	send_offsets(self.msg_vector);
	close(self.listen_sock);
}

/**********************************************************************************************************************
**********************************************************************************************************************/

void make_self_client(struct process &self, vector<struct process> process_list)
{
	struct sockaddr_in serv_addr;
	struct hostent *server;
	int i,byte_read,byte_written = -1;
	char server_ip[16];
	int server_port;
	struct message msg;
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
	cout<<"Clock at Client:"<<self.clock<<endl;
	//Connecct to Server
	if(connect(self.listen_sock,(struct sockaddr *)&serv_addr,sizeof(serv_addr))<0)
	{
		printf("\nError in Client Connection");
		return;
	}

	byte_read = read(self.listen_sock,&msg,sizeof(msg));
	cout<<"Clock at Time Daemon:"<<msg.offset<<endl;
	msg.offset = self.clock - msg.offset;
	msg.pid = self.pid;
	byte_written = write(self.listen_sock, (void *)&msg, sizeof(msg));
	cout<<"Sent offset to Time Daemon"<<endl;
	byte_read = read(self.listen_sock,&msg,sizeof(msg));
	cout<<"Recieved new offset from Time Daemon"<<endl;
	self.clock += msg.offset;
	cout<<"Synced local Clock:"<<self.clock<<endl;
	close(self.listen_sock);
}

int main(int argc, char *argv[])
{
	struct process self;
	vector<struct process> process_list;
	vector<struct process>::iterator proc_it;

	srand (time(NULL));
	self.clock = rand() % 100 + 100;
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
	if(self.pid == 1)
		make_self_server(self,process_list);
	else
		make_self_client(self,process_list);


	return 0;
}


