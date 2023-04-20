#include "policy.h"
#include <vector>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <ctime>
using namespace std;
struct TASK {
  int pr,type,art,ddl,id,Time;
  int point,iocnt,cpucnt;
  bool operator < (const TASK &A) const {
    return point < A.point;
  }
};
vector<TASK> CPUtask,IOtask;
const int N=3;
vector<TASK> task[N];
int Timelst=0;
int T=0;
Action policy(const std::vector<Event>& events, int current_cpu,
              int current_io) {
  int lenevents=events.size();
  for(int i=0;i<lenevents;++i) {
    TASK newtask;
    newtask.pr= events[i].task.priority==Event::Task::Priority::kHigh;
    switch(events[i].type) {
      case Event::Type::kTimer: newtask.type=1; break;
      case Event::Type::kTaskArrival: newtask.type=2; break;
      case Event::Type::kTaskFinish: newtask.type=3; break;
      case Event::Type::kIoRequest: newtask.type=4; break;
      case Event::Type::kIoEnd: newtask.type=5; break;
    }
    newtask.art=events[i].task.arrivalTime;
    newtask.ddl=events[i].task.deadline;
    newtask.id=events[i].task.taskId;
    newtask.Time=events[i].time;    
    if(newtask.pr==1) newtask.point=-4*(newtask.art-newtask.ddl);
    else newtask.point=-5*(newtask.art-newtask.ddl);
    if(newtask.Time>newtask.ddl) newtask.point=2e9;
    vector<TASK>::iterator itor;
    int cpucnt=0,iocnt=0;
    if(newtask.type==1) {++T;continue;}
    else if(newtask.type==2) { 
      newtask.cpucnt=0;
      newtask.iocnt=0;
      CPUtask.push_back(newtask);
    }
    else if(newtask.type==3) {
      for(itor=CPUtask.begin();itor!=CPUtask.end();itor++) 
        if(itor->id==newtask.id) {CPUtask.erase(itor); break;}
      for(itor=IOtask.begin();itor!=IOtask.end();itor++) 
        if(itor->id==newtask.id) {IOtask.erase(itor); break;}
    }
    else if(newtask.type==4) {
      for(itor=CPUtask.begin();itor!=CPUtask.end();itor++) 
        if(itor->id==newtask.id) {
          cpucnt=itor->cpucnt; 
          iocnt=itor->iocnt;
          CPUtask.erase(itor); break;}
      newtask.iocnt=iocnt;
      newtask.cpucnt=cpucnt+1;
      IOtask.push_back(newtask);
    }
    else {      
      for(itor=IOtask.begin();itor!=IOtask.end();itor++) 
        if(itor->id==newtask.id) {
          cpucnt=itor->cpucnt; 
          iocnt=itor->iocnt;
          IOtask.erase(itor); break;}
      newtask.iocnt=iocnt+1;
      newtask.cpucnt=cpucnt;
      CPUtask.push_back(newtask);
    }
  }
  if(T%20==0) {
    vector<TASK>::iterator itor;
    for(itor=CPUtask.begin();itor!=CPUtask.end();itor++)
      itor->cpucnt=0;
    for(itor=IOtask.begin();itor!=IOtask.end();itor++)
      itor->cpucnt=0;
  }
  int cpu=0,io=0;
  if(current_io) io=current_io;
  else if(!IOtask.empty()) {
    sort(IOtask.begin(),IOtask.end());
    io=IOtask[0].id;
  }
  if(!CPUtask.empty()) {
    sort(CPUtask.begin(),CPUtask.end());
    int len=CPUtask.size();

    for(int i=0;i<N;++i) task[i].clear();
    for(int i=0;i<len;++i)
      if(CPUtask[i].cpucnt<=3) task[0].push_back(CPUtask[i]);
      else if(CPUtask[i].cpucnt<=10) task[1].push_back(CPUtask[i]);
      else task[2].push_back(CPUtask[i]);

    srand(time(NULL));
    int k=rand()%N;
    while(task[k].empty()) k=rand()%N;
    cpu=task[k][0].id;
  }
  return Action{cpu,io};
}
