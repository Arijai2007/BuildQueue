// Pure presentation/request logic tests; these do not claim browser or viewport QA.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createMutationGate} from '../web/js/api.js';
import {comparisonCsv, comparisonFields} from '../web/js/exports.js';
import {gantt, memoryMap, comparisonCharts} from '../web/js/charts.js';
import {esc} from '../web/js/components.js';
let count=0;
async function test(name,fn) { await fn();count++;console.log('PASS',name); }
await test('Mutation gate rejects overlapping actions and recovers',async()=>{
    let release,calls=0;const changed=[];
    const gate=createMutationGate(()=>changed.push(gate.busy));
    const first=gate.run(async()=>{calls++;await new Promise(resolve=>release=resolve);return 7;});
    await assert.rejects(gate.run(()=>{calls++;}),/already in progress/);
    assert.equal(calls,1);assert.equal(gate.busy,true);release();assert.equal(await first,7);
    await assert.rejects(gate.run(()=>{throw new Error('transport failure');}),/transport failure/);
    assert.equal(gate.busy,false);assert.deepEqual(changed,[true,false,true,false]);
});
await test('Job text is escaped for HTML',()=>assert.equal(esc('<script>"&'), '&lt;script&gt;&quot;&amp;'));
await test('Gantt keeps RR slices, idle, historical lanes and scroll',()=>{
    const scroller={scrollLeft:53};const host={innerHTML:'',querySelector:()=>scroller};
    gantt(host,{stats:{tick:3},config:{workers:1},gantt:[
        {worker:0,job:1,start:0,end:1,dispatch:1},
        {worker:0,job:1,start:1,end:2,dispatch:2},
        {worker:0,job:0,start:2,end:3,dispatch:0},
        {worker:1,job:2,start:0,end:1,dispatch:3}
    ]});
    assert.equal((host.innerHTML.match(/class="gantt-slice /g)||[]).length,4);
    assert.match(host.innerHTML,/W2/);assert.match(host.innerHTML,/Idle/);assert.equal(scroller.scrollLeft,53);
});
await test('Memory bar renders actual owners and offsets',()=>{
    const host={innerHTML:''};memoryMap(host,{blocks:[{owner:0,offset:0,size:180},{owner:2,offset:180,size:100}]});
    assert.match(host.innerHTML,/Free.*180 MB/);assert.match(host.innerHTML,/Job 2.*\[180, 280\)/);
});
if(process.argv[2]) {
    const actual=JSON.parse(await readFile(process.argv[2],'utf8'));
    await test('CSV and JSON exports retain actual API comparison metrics',async()=>{
        const json=JSON.parse(JSON.stringify(actual));assert.deepEqual(json,actual);
        const lines=comparisonCsv(actual).trim().split('\n');
        assert.equal(lines.length,5);assert.equal(lines[0],'policy,'+comparisonFields.join(','));
        actual.results.forEach((row,i)=>{
            const cells=lines[i+1].split(',');assert.equal(cells[0],row.policy);
            comparisonFields.forEach((key,k)=>assert.equal(Number(cells[k+1]),row.stats[key]));
        });
        const rendered=comparisonCharts(actual);assert.match(rendered,/FCFS/);assert.match(rendered,/Dispatches/);
        await writeFile(process.argv[2].replace(/\.json$/,'.csv'),comparisonCsv(actual));
    });
}
console.log(`${count}/${count} frontend logic checks passed; browser layout is a separate check.`);
