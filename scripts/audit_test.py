#!/usr/bin/env python3
"""Independent API demonstration, concurrency, malformed input and lifecycle audit."""
import concurrent.futures
import csv
import io
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'buildqueue'
EVIDENCE = ROOT/'docs/evidence'
EVIDENCE.mkdir(parents=True,exist_ok=True)
results=[]
def check(value,label):
    if not value: raise AssertionError(label)
    print('PASS',label,flush=True)
def choose_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));return sock.getsockname()[1]
port=choose_port();base=f'http://127.0.0.1:{port}'
def request(path,body=None,expected=200,method=None,headers=None,raw=None):
    data=raw if raw is not None else json.dumps(body).encode() if body is not None else None
    req=urllib.request.Request(base+'/api/'+path,data=data,method=method,headers={'Content-Type':'application/json',**(headers or {})})
    try: response=urllib.request.urlopen(req,timeout=20)
    except urllib.error.HTTPError as error: response=error
    payload=json.loads(response.read())
    check(response.status==expected,f'{method or ("POST" if data is not None else "GET")} {path}: HTTP {expected}')
    check(payload.get('ok')==(expected<400),f'{path}: consistent JSON envelope')
    return payload.get('data')
def snapshot(): return request('snapshot')
def finish():
    request('config',{'delay_ms':0});request('run',{})
    for _ in range(500):
        s=snapshot()
        if not s['running']: return s
        time.sleep(.01)
    raise AssertionError('Run timed out')
def evidence(name,steps,expected,state):
    results.append({'demo':name,'steps':steps,'expected':expected,'status':'PASS','stats':state['stats'],'memory':state['memory'],'resources':state['resources'],'jobs':state['jobs'],'gantt':state['gantt'],'log':state['log']})
process=subprocess.Popen([str(EXE),'--serve','--port',str(port),'--root',str(ROOT)],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
try:
    for _ in range(100):
        try:
            urllib.request.urlopen(base+'/api/health',timeout=1).close();break
        except OSError:
            if process.poll() is not None:raise RuntimeError(process.stdout.read().decode())
            time.sleep(.05)
    conflict=subprocess.run([str(EXE),'--serve','--port',str(port),'--root',str(ROOT)],cwd=ROOT,capture_output=True,text=True,timeout=10)
    check(conflict.returncode==1 and 'Cannot bind' in conflict.stderr,'Port conflict returns a useful startup error')
    for body in [{},{'id':0},{'id':True},{'id':1.2},{'id':18446744073709551615}]:request('cancel',body,400)
    for identifier in ['+1','-1','abc','999999999999999999999999'] : request('jobs/'+identifier,expected=400)
    request('workers',{},400)
    request('config',{'memory':1048576})
    check(request('memory')['total']==1048576,'Documented maximum capacity is reachable through HTTP')
    request('submit',{},415,headers={'Content-Type':'application/json-invalid'})
    request('config',{},method='PUT',expected=405)
    request('not-a-route',expected=404)
    request('submit',raw=b'{' + b' '*65536,expected=413)
    request('reset',{});request('config',{'memory':1024})

    request('load-demo',{'name':'fcfs'});s=finish()
    check([j['ready_wait'] for j in s['jobs']]==[0,3,5],'FCFS individual waiting times')
    check([j['turnaround'] for j in s['jobs']]==[4,6,7],'FCFS individual turnaround times')
    check(abs(s['stats']['avg_wait']-8/3)<1e-10 and abs(s['stats']['avg_turnaround']-17/3)<1e-10,'FCFS unrounded average metrics')
    evidence('First things first',['load-demo fcfs','run until complete'],'Waits 0/3/5; turnaround 4/6/7; Gantt 0–4/4–7/7–9',s)

    request('load-demo',{'name':'round_robin'});request('step',{});request('step',{});s=snapshot()
    check(s['jobs'][0]['state']=='READY' and s['jobs'][0]['remaining']==3,'RR preempts after two ticks preserving remaining burst')
    check(s['memory']['used']==192 and s['resources']['active_slots']==0,'RR retains 192 MB reservation and releases slot')
    evidence('A fair share',['load-demo round_robin','step','step'],'First job READY with 3 ticks remaining; 192 MB retained; 1 preemption',s)

    request('load-demo',{'name':'fragmentation'})
    for _ in range(3):request('step',{})
    s=snapshot();check(s['jobs'][4]['state']=='WAITING' and s['jobs'][4]['wait_reason']=='MEMORY_UNAVAILABLE','Fragmentation uses actual WAITING reason')
    check(s['memory']['free']==580 and s['memory']['largest_free']==300,'580 MB free but largest hole only 300 MB')
    evidence('Space, scattered',['load-demo fragmentation','step three times'],'400 MB request WAITING with holes 180/100/300 MB',s)

    request('load-demo',{'name':'synchronization'});request('step',{});s=snapshot()
    check(s['config']['workers']==4 and s['resources']['active_slots']==2 and s['stats']['waiting']==2,'Four logical workers share two real semaphore permits')
    final=finish();check(final['stats']['completed']==5 and final['resources']['peak_slots']==2 and final['resources']['shared_counter']==18,'All blocked jobs progress; peak permits 2; atomic counter 18')
    evidence('Controlled concurrency',['load-demo synchronization','step','run until complete'],'2 holders, 2 blocked at tick 1; all 5 finish; counter 18',final)

    request('load-demo',{'name':'sjf'});before=snapshot();a=request('compare',{});b=request('compare',{})
    check(a==b,'SJF scenario comparisons replay identically')
    check(before==snapshot(),'Comparison leaves original experiment unchanged')
    check(a['workload_size']==4 and len(a['results'])==4,'All policies receive the same four input jobs')
    (EVIDENCE/'comparison.json').write_text(json.dumps(a,indent=2)+'\n')
    fields=['avg_wait','avg_turnaround','avg_response','cpu_utilization','throughput','context_switch_like_transitions']
    stream=io.StringIO();writer=csv.writer(stream);writer.writerow(['policy',*fields])
    for row in a['results']:writer.writerow([row['policy'],*(row['stats'][field] for field in fields)])
    (EVIDENCE/'comparison.csv').write_text(stream.getvalue())
    parsed=list(csv.DictReader(io.StringIO(stream.getvalue())))
    for row,actual in zip(parsed,a['results']):
        for field in fields:check(float(row[field])==actual['stats'][field],f'Export {actual["policy"]} {field} matches displayed source metric')
    evidence('The shortest path',['load-demo sjf','compare twice','export and parse JSON/CSV'],'Four identical-input deterministic runs; exports preserve all table metrics',before)
    results[-1]['comparison']=a

    request('load-demo',{'name':'memory_exhaustion'});request('step',{});s=snapshot()
    waiter=next(job for job in s['jobs'] if job['state']=='WAITING')
    request('cancel',{'id':waiter['id']});after=snapshot()
    check(after['memory']['used']==s['memory']['used'] and after['resources']['active_slots']==s['resources']['active_slots'],'Cancelling a WAITING job leaves running owners intact')
    request('cancel',{'id':waiter['id']},400)
    final=finish();check(final['stats']['completed']==2 and final['stats']['cancelled']==1 and final['memory']['used']==0,'WAITING cancellation and subsequent completion reclaim resources')

    request('reset',{})
    def submit(i): return request('submit',{'name':f'Concurrent {i}','burst':1,'memory':16},201)['id']
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as executor:ids=list(executor.map(submit,range(24)))
    check(len(set(ids))==24,'Concurrent HTTP submissions produce 24 unique IDs')
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as executor:states=list(executor.map(lambda _:snapshot(),range(12)))
    check(all(len(s['jobs'])==24 for s in states),'Concurrent snapshots contain coherent job registries')
    for _ in range(12):
        request('load-demo',{'name':'mixed_workload'});request('step',{});request('reset',{});s=snapshot()
        check(s['resources']['active_slots']==0 and s['memory']['used']==0 and not s['jobs'],'Repeated scenario reset releases reservations and permits')
finally:
    process.terminate();process.wait(timeout=10)
check(process.returncode==0,'Audit server shutdown is clean')
for cycle in range(3):
    port=choose_port();base=f'http://127.0.0.1:{port}'
    child=subprocess.Popen([str(EXE),'--serve','--port',str(port),'--root',str(ROOT)],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:urllib.request.urlopen(base+'/api/health',timeout=1).close();break
            except OSError:time.sleep(.03)
        else:raise AssertionError('Repeated startup timed out')
    finally:child.terminate();child.wait(timeout=10)
    check(child.returncode==0,f'Startup/shutdown cycle {cycle+1}')
(EVIDENCE/'demo-results.json').write_text(json.dumps(results,indent=2)+'\n')
print('All independent audit checks passed. Evidence: docs/evidence/',flush=True)
