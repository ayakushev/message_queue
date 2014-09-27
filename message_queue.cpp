// message_queue.cpp : Defines the entry point for the console application.
//
#include <iostream>
#include <time.h>

#include "message_queue.h"

using namespace std;

//
//auxiliary method that returns tick count
//
unsigned getTickCount()
{
#ifdef _WIN32
	return GetTickCount();
#else
	struct timeval tv;
	gettimeofday(&tv, 0);
	return unsigned((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
#endif
}
unsigned _firstTick=getTickCount();

//
//auxiliary function to print trace into error stream 
//
void PrtLog(const char* fmt_str, ...)
{
	char buf[1024];

	sprintf_s(buf,1024,"%.3f : ",(double)(getTickCount()-_firstTick)/1000);

	va_list marker;
	va_start (marker, fmt_str );
	vsnprintf_s(buf+strlen(buf), sizeof(buf)-strlen(buf), _TRUNCATE, fmt_str, marker );
	va_end  ( marker );
	

	fprintf( stderr, buf );
}

//////////////////////////////////////////////////////////////////////////
template<typename T> MessageQueue<T>::~MessageQueue()
{
	PrtLog("MessageQueue::~MessageQueue [0x%x]\n",this);
	//notify about stop
	_cv.notify_all();
}

//sends message to queue with using a priority
template<typename T> RetCode MessageQueue<T>::put(const T& message, int priority)
{
	// safe work under _mutix
	tthread::lock_guard<tthread::recursive_mutex> lock(_mutex);

	//Put message and notify about that for waited reader
	Msg msg( message, priority );
	_queue.push(msg);
	if(_queue.size()==1)
		_cv.notify_one();

	//invokes event
	if( pEvent && _queue.size() >= (unsigned)hwm )
		pEvent->on_hwm();

	return OK;
}

//obtains message from queue 
template<typename T> RetCode MessageQueue<T>::get(T &message)
{
	tthread::lock_guard<tthread::recursive_mutex> lock(_mutex);
	
	// checks that queue is stopped
	if( isStopped() )
		return RetCode::STOPPED;

	//abort in case if queue is empty
	while( _queue.size() == 0 )
	{
		//waits signal to continue
		_cv.wait(_mutex);
		if( _queue.size() == 0 && isStopped() )
		{
			PrtLog("MessageQueue::get [0x%x] aborts after stop\n");
			return RetCode::STOPPED;
		}
		PrtLog("MessageQueue::get [0x%x] continues wait\n");
	}

	//get message data from queue 
	{
		message = _queue.top().get_data();
		_queue.pop();

		//invokes event
		if( pEvent && _queue.size() <= (unsigned)lwm )
			pEvent->on_lwm();
	}

	return OK;
}

//invokes all about start
template<typename T> void MessageQueue<T>::run()
{
	tthread::lock_guard<tthread::recursive_mutex> lock(_mutex);

	if(pEvent)
		pEvent->on_start();
	
	return;
}

//stops to work
template<typename T> void MessageQueue<T>::stop()
{
	tthread::lock_guard<tthread::recursive_mutex> lock(_mutex);

	bIsStopped = true;

	//sends about stop to writer
	if(pEvent)
		pEvent->on_stop();

	//removes all elements in queue
	while(_queue.size()>0)
		_queue.pop();
	//_queue.c.empty();

	_cv.notify_all();
}

//sets callback for events
template<typename T> void MessageQueue<T>::set_event(IMessageQueueEvent *pIMessageQueueEvent)
{
	//_lock:
	pEvent = pIMessageQueueEvent;
}


//////////////////////////////////////////////////////////////////////////
template<typename T>
Writer<T>::~Writer()
{
	if(pThread)
	{
		pThread->join();
		delete pThread;
	}
}

template<typename T>
void Writer<T>::run_safe(void *param)
{
	char s[100];
	Writer<T> *pThis = (Writer<T>*)param;
	if(!pThis)
		return;
	
	pThis->_stat = RUN;
	
	// Seed the random-number generator with current thread address so that
	// the numbers will be different in each thread.
	srand( (int)pThis );

	while(true)
	{
		if( pThis->pQueue->isStopped() )
		{
			PrtLog("Writer::run_safe [0x%x] has been aborted\n",pThis);
			break;
		}
		if( pThis->_stat==PAUSED )
		{
			PrtLog("Writer::run_safe [0x%x] is waiting\n",pThis);
			//waiting resume
			//I'm suspending that need to change with event object to signal about resume (WaitForSingleObject(...))
			//but now it is implemented via Sleep
			Sleep(100);
			continue;
		}

		//generates message by current type
		T message; 

		//gets random num, use this for more difference between threads
		int r = rand();
		int priority = r % (int)1000;

		if( typeid(T) == typeid(string) )
		{
			sprintf_s(s,"string msg %d by [0x%x]", priority, pThis );
			message=s;
			PrtLog("Writer[0x%x] puts: %s\n",pThis,s);
		}
		else if( typeid(T) == typeid(int) )
		{
			message = priority;
			PrtLog("Writer[0x%x] puts: %d\n",pThis,s);
		}
		else 
		{
			//does not support this type;
			break;
		}

		//write message to queue
		if(pThis->pQueue)
			pThis->pQueue->put(message,priority);

		//timeout between write operations
		Sleep(0);
	}
}

//starts write messages to queue
template<typename T> void Writer<T>::run()
{
	if(pThread)
	{
		//for reuse tun() need to waits previous thread and delete it
		pThread->join();
		delete pThread;
	}

	//create a new thread
	pThread = new tthread::thread(run_safe,this);
	return;
}


//////////////////////////////////////////////////////////////////////////
// class Reader
//thread procedure that run Writer under thread
template<typename T>
void Reader<T>::run_safe(void *param)
{
	Reader<T> *pThis = (Reader<T>*)param;
	PrtLog("Reader::run_safe [0x%x] begin\n",pThis);
	if(!pThis)
		return;

	while(pThis->pQueue)
	{
		T msg;

		//waits a moment when queue is not empty and returns message from queue
		RetCode ret=pThis->pQueue->get(msg);

		//checks a code, abort if it is stopped
		if( ret == RetCode::STOPPED )
			break;

		//processes message
		pThis->handle_message(msg);

		//Wait time that was corresponded in constructor
		Sleep(pThis->timeout);
	}
	PrtLog("Reader::run_safe [0x%x] end\n",pThis);
}

template<typename T>
void Reader<T>::run()
{
	if(pThread)
	{
		//for reuse tun() need to waits previous thread and delete it
		pThread->join();
		delete pThread;
	}

	//create a new thread
	pThread = new tthread::thread(run_safe,this);
	return;
}

//processes message
template<typename T>
void Reader<T>::handle_message(const T& msg)
{
#if 1 //additional log to stderr
 	if( typeid(T) == typeid(string) )
 		PrtLog("Reader::handle_message[0x%x] %s\n",this,msg.c_str());
 	else if( typeid(T) == typeid(int) )
 		PrtLog("Reader::handle_message [0x%x] %d\n'",this,msg);
#endif

	//just print it with corresponded prefix string
	if( typeid(T) == typeid(string) )
		printf("%s %s\n",s_prefix.c_str(),msg.c_str());
	else if( typeid(T) == typeid(int) )
		printf("%s %d\n",s_prefix.c_str(),msg);
	
	//cout << s_prefix << msg << endl;
}

//////////////////////////////////////////////////////////////////////////
// class EventManager translates events from MessageQueue to each Writer 
//
// collects all pointers to interface
// then calls each corresponded pointers via own implemented functions of interface
// 
int EventManager::Add(IMessageQueueEvent *pEvent)
{
	events.push_back(pEvent);
	return events.size();
}

void EventManager::on_start()
{
	PrtLog("%s\n",__FUNCTION__);

	for( vector<IMessageQueueEvent*>::iterator it = events.begin() ; it != events.end(); it++)
		(*it)->on_start();
}
void EventManager::on_stop()
{
	PrtLog("%s\n",__FUNCTION__);

	for( vector<IMessageQueueEvent*>::iterator it = events.begin() ; it != events.end(); it++)
		(*it)->on_stop();
}
void EventManager::on_hwm()
{
	PrtLog("%s\n",__FUNCTION__);

	for( vector<IMessageQueueEvent*>::iterator it = events.begin() ; it != events.end(); it++)
		(*it)->on_hwm();
}
void EventManager::on_lwm()
{
	PrtLog("%s\n",__FUNCTION__);

	for( vector<IMessageQueueEvent*>::iterator it = events.begin() ; it != events.end(); it++)
		(*it)->on_lwm();
}

//////////////////////////////////////////////////////////////////////////
// MAIN
int main()
{
	//we define that will be working with type of message is equal string:
	typedef string MessageType;

	//create the Queue object
	MessageQueue<MessageType> q(100,10,90);
	
	EventManager event_mgr;
	q.set_event(&event_mgr);
	
	//Create three writers
	Writer<MessageType> writer1(&q);
	Writer<MessageType> writer2(&q);
	Writer<MessageType> writer3(&q);
	
	//init EventManager that will translate events from MessageQueue to each Writer instance
	event_mgr.Add(&writer1);
	event_mgr.Add(&writer2);
	event_mgr.Add(&writer3);

	//create two reader objects that will read message from queue
	Reader<MessageType> reader1( &q );
	Reader<MessageType> reader2( &q );

	//run to write messages from three objects
	writer1.run();
	writer2.run();
	writer3.run();

	//waits 2 sec while queue will be full
	Sleep(2000);

	PrtLog("**run readers\n");

	//starts read and in the same time writers also puts new message to queue
	reader1.run();
	reader2.run();

	PrtLog("**run sleep\n");

	//looking what happens during 10 sec
	Sleep(5000);

	PrtLog("**run stop\n");

	//stopping all
	q.stop();

	return 0;
}