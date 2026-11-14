export const esc = s => String(s).replace(
    /[&<>"']/g,
    c => ({'&' : '&amp;', '<' : '&lt;', '>' : '&gt;', '"' : '&quot;', "'" : '&#39;'}[c]));
export const fmt = (n, d = 0) =>
    Number(n).toLocaleString(undefined, {maximumFractionDigits : d, minimumFractionDigits : d});
export const colors =
    [ '#8175f5', '#38bfc9', '#e7ad61', '#da83b5', '#77b78b', '#7fa9e5', '#ce9474', '#b59ce7' ];
export const color = id => colors[(id - 1) % colors.length];
const paths = {
    overview : 'M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z',
    jobs : 'M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01',
    cpu : 'M7 7h10v10H7zM9 1v3M15 1v3M9 20v3M15 20v3M1 9h3M1 15h3M20 9h3M20 15h3',
    memory : 'M3 6h18v12H3zM7 10v4M11 10v4M15 10v4M7 18v3M12 18v3M17 18v3',
    resources : 'M4 8h16M4 16h16M8 4v8M16 12v8',
    chart : 'M4 3v17h17M8 15v-5M13 15V6M18 15v-8',
    logs : 'M5 3h14v18H5zM9 7h6M9 11h6M9 15h4',
    info : 'M12 16v-4M12 8h.01M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0',
    sun :
        'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1 1M18 18l1 1M5 19l1-1M18 6l1-1',
    reset : 'M3 10a9 9 0 1 1 2 8M3 4v6h6',
    layers : 'm12 3 10 5-10 5L2 8zM2 12l10 5 10-5M2 16l10 5 10-5',
    plus : 'M12 5v14M5 12h14',
    play : 'm8 4 12 8-12 8z',
    pause : 'M8 5v14M16 5v14',
    step : 'm5 4 10 8-10 8zM19 4v16',
    spark : 'm12 3 2.5 6.5L21 12l-6.5 2.5L12 21l-2.5-6.5L3 12l6.5-2.5z',
    flask : 'M9 3h6M10 3v6L4 19q-1 2 2 2h12q3 0 2-2L14 9V3M7 15h10'
};
export function icon(name) {
    return `<svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="${
        paths[name] || paths.layers}"/></svg>`;
}
export function mountIcons(root = document) {
    root.querySelectorAll('[data-icon]').forEach(el => { el.innerHTML = icon(el.dataset.icon); });
}
export function badge(state) {
    return `<span class="state-pill state-${state.toLowerCase()}">${state}</span>`;
}
export function empty(title, detail = '') {
    return `<div class="empty-state">${icon('layers')}<h3>${esc(title)}</h3><p>${
        esc(detail)}</p></div>`;
}
export const demoInfo = {
    fcfs : [ 'First things first', 'Arrival-order execution with hand-calculated metrics.' ],
    sjf : [ 'The shortest path', 'See small jobs finish sooner under non-preemptive SJF.' ],
    priority :
        [ 'Priority matters', 'Lower numeric priority wins; stable ties by arrival and ID.' ],
    round_robin : [ 'A fair share', 'Two-tick quanta, FIFO requeue, and boundary arrivals.' ],
    memory_first_fit : [ 'First Fit', 'The first suitable hole receives the 90 MB probe.' ],
    memory_best_fit : [ 'Best Fit', 'The smallest suitable hole receives the probe.' ],
    memory_worst_fit : [ 'Worst Fit', 'The largest hole receives the probe.' ],
    fragmentation : [ 'Space, scattered', '580 MB is free, but a 400 MB allocation must wait.' ],
    memory_exhaustion : [ 'At capacity', 'Resource-blocked jobs resume after memory is released.' ],
    synchronization :
        [ 'Controlled concurrency', 'Four workers compete for two shared build slots.' ],
    mixed_workload :
        [ 'Everything, connected', 'Scheduling, memory pressure, and semaphore contention.' ]
};
