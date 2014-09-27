#ifndef _MESSAGE_QUEUE_H_
#define _MESSAGE_QUEUE_H_

#include <queue>
#include <string>
#include "TinyThread/tinythread.h"

using namespace std;

//////////////////////////////////////////////////////////////////////////
//interface for MessageQueue events mechanism
class IMessageQueueEvent
{
public:
	virtual void on_start()=0;
	virtual void on_stop()=0;
	virtual void on_hwm()=0;
	virtual void on_lwm()=0;
}; 

//////////////////////////////////////////////////////////////////////////
//class to keep message that has T type and priority
//
template<typename T> 
class Message   
{
	// message 
	T data;
public:
	//Priority of message 
	unsigned short priority;
	// ctr
	Message(const Message& msg) : priority(msg.priority), data(msg.data) {  }
	// ctr
	explicit Message(const T& data,unsigned short priority): data(data), priority(priority){ 	}
	// operator =
	Message& operator=(const Message& msg) 
	{
		data=msg.data;
		priority=msg.priority;
		return *this;
	}
	// get data of message
	const T& get_data() const
	{
		return data;
	}
	//to support sorting compare function for priority_queue for less and greater case
	const bool operator < ( const Message &r ) const
	{
		bool bRet = ( priority < r.priority );
		// trace sorting
		//cout << get_data() << "-" << hex << (int)priority << " and " << r.get_data() << "-" << hex << (int)r.priority << "  bool = " << bRet << endl;
		return bRet;
	}
	const bool operator > ( const Message &r ) const
	{
		return ( priority > r.priority );
	}
};

//available return codes
enum RetCode{
	OK=0,
	HWM=-1,
	NO_SPACE=-2,
	STOPPED=-3
};

//////////////////////////////////////////////////////////////////////////
// class MessageQueue creates sorting queue by priority and with blocking read for asynchronous thread model 
// Supports lwm and hwm that are indicated by events
// ::get method with blocking mechanism that will return after new message in queue or after stopping of work
// 
template<typename T>
class MessageQueue
{
	// the recursive_mutex uses WinApi CriticalSection functions for Win32 and pthread_mutex_* functions for POSIX
	tthread::recursive_mutex _mutex;
	// the queue with blocking get, so we use condition_variable to notify from get to put
	tthread::condition_variable _cv; 

	//parameters of queue
	int hwm,lwm,queue_size;
	
	//pointer to interface
	IMessageQueueEvent *pEvent;
	
	typedef Message<T> Msg;
	priority_queue< Msg, vector<Msg>, less<Msg> > _queue;

	bool bIsStopped;
public:
		
	//ctr
	MessageQueue(int queue_size, int lwm, int hwm):queue_size(queue_size),lwm(lwm),hwm(hwm),pEvent(NULL),bIsStopped(false)	{	};

	//dstr
	~MessageQueue();

	//sends message to queue by using corresponded priority
	RetCode put(const T& message, int priority);

	//obtains message from queue 
	RetCode get(T &message);

	//creates thread that calls run_safe
	void run();

	//stops to work
	void stop();

	//sets callback for events
	void set_event(IMessageQueueEvent *pIMessageQueueEvent);

	//get status
	const bool isStopped() const
	{ return bIsStopped; }

};

//////////////////////////////////////////////////////////////////////////
//  class Writer
//  
//  The owner creates Writer object by constructor that must have pointer on MessageQueue 
//  then owner calls run to start. 
//  After that object creates thread that generates messages and checks status that can be paused, resumed or aborted 
//  The control of status is happened in implemented functions of IMessageQueueEvent interface
//
template<typename T>
class Writer : public IMessageQueueEvent 
{
	MessageQueue<T> *pQueue;
	tthread::thread *pThread;

	enum WriterStat{
		IDLE=0,
		RUN,
		PAUSED,
		ABORTED
	};
	WriterStat _stat;
public:
 	explicit Writer(MessageQueue<T>* pQueue) :pThread(NULL), pQueue(pQueue), _stat(IDLE){	}
	//destructor
	~Writer();
private:	
	//thread procedure that run Writer under thread
	static void run_safe(void *param);

public:

	//starts write messages to queue
	void run();

public:
	void on_start()
	{
		run();
	}
	void on_stop()
	{
		//stops write messages to queue
		_stat=ABORTED;
	}
	void on_hwm()
	{
		//makes a pause in writing 
		if(_stat==RUN)
			_stat=PAUSED;
	}
	void on_lwm()
	{
		//resumes work
		if(_stat==PAUSED)
			_stat=RUN;
	}
};


//////////////////////////////////////////////////////////////////////////
// class Reader
//  
//  The owner creates Reader object by constructor that must have pointer on MessageQueue 
//  then owner calls run to start. 
//  After that object creates thread that starts to read messages from queue that are processed in handle_message method 
//    
//

template<typename T>
class Reader
{
	int timeout;
	MessageQueue<T> *pQueue;
	string s_prefix;
	tthread::thread *pThread;

public:
	
	explicit Reader( MessageQueue<T>* pQueue, int timeout_between_reading = 0, const char *scPrefizxForPrinting = NULL)
		: pThread(NULL),
		pQueue(pQueue),
		timeout(timeout_between_reading)
	{
		if(scPrefizxForPrinting)
			s_prefix = scPrefizxForPrinting;
		else
		{
			char s[100]; 
			sprintf_s(s,"Reader[0x%x]:",this); 
			s_prefix = s;
			//s_prefix << "Reader(0x" << setbase(16) << (int)this << "):";
		}
	}

	//destructor
	~Reader()
	{
		if(pThread)
		{
			pThread->join();
			delete pThread;
		}
	}

	//thread procedure that is ran under thread
	static void run_safe(void *param);
	//creates thread
	void run();

protected:
	//processes message
	void handle_message(const T& msg);
};

//////////////////////////////////////////////////////////////////////////
// class EventManager translates events from MessageQueue to each Writer 
//
// collects all pointers to interface
// then calls each corresponded pointers via own implemented functions of interface
// 
class EventManager :  public IMessageQueueEvent
{
	vector<IMessageQueueEvent*> events;
public:
	int Add(IMessageQueueEvent *pEvent);
public:
	void on_start();
	void on_stop();
	void on_hwm();
	void on_lwm();
};


#endif _MESSAGE_QUEUE_H_