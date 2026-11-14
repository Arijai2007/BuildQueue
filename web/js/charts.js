import {color, empty, esc, fmt} from './components.js';

export function gantt(host, s, zoom = 36) {
    const scroll = host.querySelector('.gantt-scroll')?.scrollLeft || 0;
    const events = s.gantt, tick = s.stats.tick;
    if (!events.length) {
        host.innerHTML = empty('A clear runway',
                               'Load a demo and press Step or Run to see the execution timeline.');
        return;
    }
    const width = Math.max(tick * zoom + 40, 450);
    const groups = [];
    // Show dispatch boundaries, even when the same job is selected again.
    const laneCount = events.reduce((n, e) => Math.max(n, e.worker + 1), s.config.workers);
    for (let w = 0; w < laneCount; w++) {
        const slices = [];
        for (const e of events) {
            if (e.worker !== w)
                continue;
            const last = slices.at(-1);
            if (last && last.job === e.job && last.end === e.start && last.dispatch === e.dispatch)
                last.end = e.end;
            else
                slices.push({...e});
        }
        groups.push(`<div class="gantt-lane"><span class="lane-label">W${
            w + 1}</span><div class="lane-track" style="width:${width}px">${
            slices
                .map(e => `<div class="gantt-slice ${e.job ? '' : 'idle'}" style="left:${
                         e.start * zoom}px;width:${(e.end - e.start) * zoom - 2}px;--job-color:${
                         e.job ? color(e.job)
                               : '#657086'}" title="${e.job ? 'Job ' + e.job : 'Idle'} · worker ${
                         w + 1} · ticks ${e.start}–${e.end}">${e.job ? 'J' + e.job : 'idle'}</div>`)
                .join(
                    '')}<div class="live-cursor" style="left:${tick * zoom}px"></div></div></div>`);
    }
    const stride = Math.max(1, Math.ceil(36 / zoom), Math.ceil(tick / 100));
    const ticks = [];
    for (let t = 0; t <= tick; t += stride)
        ticks.push(`<span style="left:${t * zoom}px">${t}</span>`);
    host.innerHTML = `<div class="gantt-scroll"><div class="gantt-inner" style="min-width:${
        width + 48}px"><div class="gantt-ruler" style="width:${width}px">${ticks.join('')}</div>${
        groups.join('')}</div></div>`;
    host.querySelector('.gantt-scroll').scrollLeft = scroll;
}
export function memoryMap(host, m) {
    host.innerHTML =
        m.blocks
            .map(b => `<div class="memory-block ${b.owner ? 'allocated' : 'free'}" style="flex:${
                     b.size};--job-color:${b.owner ? color(b.owner) : '#344055'}" title="${
                     b.owner ? 'Job ' + b.owner : 'Free'} · ${b.size} MB · [${b.offset}, ${
                     b.offset + b.size}) MB"><span>${b.owner ? 'J' + b.owner : 'Free'}<small>${
                     fmt(b.size)} MB</small></span></div>`)
            .join('');
}
export function comparisonCharts(result) {
    const rows = result.results;
    const metrics = [
        [ 'avg_wait', 'Ready waiting time', 'ticks' ],
        [ 'avg_turnaround', 'Turnaround time', 'ticks' ],
        [ 'avg_response', 'Response time', 'ticks' ]
    ];
    return `<div class="comparison-charts">${
        metrics
            .map(([ key, label, unit ]) => {
                const max = Math.max(1, ...rows.map(r => r.stats[key]));
                return `<section><h3>${label} <small>${unit}</small></h3>${
                    rows.map((r, i) => `<div class="bar-chart-row"><b>${
                                 r.policy.toUpperCase()}</b><div><span style="width:${
                                 100 * r.stats[key] /
                                 max}%;background:${color(i + 1)}"></span></div><strong>${
                                 fmt(r.stats[key], 2)}</strong></div>`)
                        .join('')}</section>`;
            })
            .join(
                '')}</div><div class="table-scroll"><table><thead><tr><th>Policy</th><th>Ready wait</th><th>Turnaround</th><th>Response</th><th>CPU %</th><th>Jobs/tick</th><th>Dispatches</th></tr></thead><tbody>${
        rows.map(r => `<tr><td><strong>${esc(r.policy.toUpperCase())}</strong></td><td>${
                     fmt(r.stats.avg_wait, 3)}</td><td>${fmt(r.stats.avg_turnaround, 3)}</td><td>${
                     fmt(r.stats.avg_response,
                         3)}</td><td>${fmt(r.stats.cpu_utilization, 1)}</td><td>${
                     fmt(r.stats.throughput,
                         3)}</td><td>${r.stats.context_switch_like_transitions}</td></tr>`)
            .join('')}</tbody></table></div><p class="panel-note">${
        result.workload_size} identical jobs · ${result.configuration.workers} worker(s) · ${
        result.configuration.slots} slot(s) · ${fmt(result.configuration.memory)} MB · RR quantum ${
        result.configuration
            .quantum}. Dispatches are context-switch-like simulation transitions, not kernel context switches.</p>`;
}
