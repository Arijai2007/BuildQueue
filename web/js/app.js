import {api, createMutationGate, download} from './api.js';
import {comparisonCharts, gantt, memoryMap} from './charts.js';
import {badge, color, demoInfo, empty, esc, fmt, icon, mountIcons} from './components.js';
import {comparisonCsv} from './exports.js';

const $ = id => document.getElementById(id);
let state = null, comparison = null, refreshPromise = null, toastTimer, page = 'overview';
let requestEpoch = 0, online = false;
const mutations = createMutationGate(updateControls);
function updateControls() {
    const blocked = mutations.busy || !online;
    document.querySelectorAll('button').forEach(button => {
        if (button.id === 'theme-toggle' || button.hasAttribute('data-close') ||
            button.id.startsWith('export-'))
            return;
        button.disabled = blocked;
    });
    const unfinished =
        state && state.stats.total - state.stats.completed - state.stats.cancelled > 0;
    $('run-btn').disabled = blocked || !unfinished;
    $('step-btn').disabled = blocked || !unfinished || state?.running;
    $('compare-btn').disabled = blocked || !state?.jobs.length;
    document.querySelectorAll('form input,form select,form button[type=submit]')
        .forEach(input => input.disabled = blocked);
    $('theme-toggle')
        .setAttribute('aria-pressed', String(document.documentElement.dataset.theme === 'light'));
}
function clearDrafts() {
    for (const id of ['policy-form', 'memory-policy-form', 'resource-form', 'delay-form'])
        delete $(id).dataset.dirty;
}
const pageMeta = {
    overview : [
        'Your system, in focus.', 'Schedule intelligently. Allocate precisely. See every decision.',
        'Overview'
    ],
    jobs : [
        'Every job has a story.',
        'Submit, inspect, and manage the work moving through your system.', 'Jobs'
    ],
    scheduler : [
        'Make every tick count.', 'Explore how each policy shapes the execution timeline.',
        'CPU Scheduler'
    ],
    memory : [
        'A place for every job.', 'Watch contiguous memory split, fill, release, and merge.',
        'Memory Manager'
    ],
    resources : [
        'Concurrency, under control.',
        'Observe the critical sections that keep shared resources consistent.', 'Resource Monitor'
    ],
    comparison : [
        'See the trade-offs.', 'Compare four strategies on one reproducible workload.',
        'Algorithm Comparison'
    ],
    logs : [
        'Nothing behind the curtain.',
        'A chronological record of the decisions that drive your system.', 'Event Logs'
    ],
    project : [
        'A system built to explain.',
        'Six operating systems concepts. One connected local pipeline.', 'Project Information'
    ]
};
const policyDescriptions = {
    fcfs :
        'Non-preemptive. Selects the earliest arrival, then the lowest job ID. An admitted job keeps its lane until completion. Resource-blocked jobs are skipped so other eligible jobs can progress.',
    sjf :
        'Non-preemptive. Selects the shortest original CPU burst among READY jobs, then arrival and job ID. Future short jobs do not interrupt a running job. No prediction of actual Linux process duration is implied.',
    priority :
        'Non-preemptive. Lower numeric priority wins; arrival and job ID break ties. Continuous high-priority arrivals can starve low-priority work; aging is not enabled in this implementation.',
    rr :
        'Preemptive simulation. FIFO ready queue with bounded quanta. Arrivals at a boundary join before unfinished jobs are requeued. Memory is retained across quanta; build slots are released. Live mode does not support RR.'
};
function toast(message, error = false) {
    clearTimeout(toastTimer);
    $('toast').textContent = message;
    $('toast').className = 'toast' + (error ? ' error' : '');
    $('toast').hidden = false;
    toastTimer = setTimeout(() => $('toast').hidden = true, 4500);
}
function text(id, value) {
    $(id).textContent = value;
}
function syncValue(id, value) {
    if (document.activeElement !== $(id) && !$(id).closest('form')?.dataset.dirty)
        $(id).value = value;
}
function go() {
    page = location.hash.slice(1);
    if (!pageMeta[page])
        page = 'overview';
    document.querySelectorAll('.page').forEach(el => el.hidden = el.id !== `page-${page}`);
    document.querySelectorAll('[data-page]').forEach(el => {
        el.classList.toggle('active', el.dataset.page === page);
        if (el.dataset.page === page)
            el.setAttribute('aria-current', 'page');
        else
            el.removeAttribute('aria-current');
    });
    const [title, description, crumb] = pageMeta[page];
    text('page-title', title);
    text('page-description', description);
    text('breadcrumb', crumb);
    window.scrollTo(0, 0);
    if (state)
        render();
}
function kpi(label, value, unit, context, name = 'chart') {
    return `<div class="kpi"><div class="kpi-title">${label}${
        icon(name)}</div><div class="kpi-value">${value}${
        unit ? ` <small>${unit}</small>`
             : ''}</div><div class="kpi-context"><span class="tiny-line"></span>${
        context}</div></div>`;
}
function renderJobs() {
    if (!state)
        return;
    const search = $('job-search').value.toLowerCase(), filter = $('job-filter').value,
          sort = $('job-sort').value;
    const jobs = state.jobs
                     .filter(j => (!filter || j.state === filter) &&
                                  `${j.id} ${j.name}`.toLowerCase().includes(search))
                     .sort((a, b) => typeof a[sort] === 'number'
                                         ? a[sort] - b[sort]
                                         : String(a[sort]).localeCompare(String(b[sort])));
    $('job-table').innerHTML =
        jobs.length
            ? jobs.map(j => `<tr><td><span class="job-id">J${j.id}</span></td><td><strong>${
                           esc(j.name)}</strong><div class="muted" style="font-size:8px">${
                           esc(j.type)}${
                           j.process_pid > 0
                               ? ' · PID ' + j.process_pid + ' · exit ' + j.process_exit
                               : ''}</div>${
                           j.process_error ? `<div class="form-error">${esc(j.process_error)}</div>`
                                           : ''}</td><td>${j.arrival}</td><td>${j.burst}</td><td>${
                           j.remaining}</td><td>${j.priority}</td><td>${j.memory} MB</td><td>${
                           badge(j.state)}${
                           j.wait_reason
                               ? `<div class="job-wait-reason">${
                                     esc(j.wait_reason.replaceAll('_', ' '))}</div>`
                               : ''}</td><td>${j.worker < 0 ? '—' : 'W' + (j.worker + 1)}</td><td>${
                               ['NEW', 'READY', 'WAITING'].includes(j.state)
                                   ? `<button class="cancel-button" data-cancel="${
                                         j.id}">Cancel</button>`
                                   : '—'}</td></tr>`)
                  .join('')
            : `<tr><td colspan="10">${
                  empty('No jobs to show',
                        state.jobs.length
                            ? 'Try another search or state filter.'
                            : 'Submit a job or load a demo to get started.')}</td></tr>`;
}
function renderKanban() {
    const labels = [ 'READY', 'RUNNING', 'WAITING', 'COMPLETED' ];
    $('kanban').innerHTML =
        labels
            .map(label => {
                const jobs = state.jobs.filter(j => j.state === label);
                return `<div class="kanban-column"><div class="kanban-heading"><span class="queue-dot"></span>${
                    label}<span class="count">${jobs.length}</span></div>${
                    jobs.slice(0, 5)
                        .map(
                            j => `<article class="job-card" style="--job-color:${
                                color(j.id)}"><header><b>J${j.id}</b><strong title="${
                                esc(j.name)}">${esc(j.name)}</strong></header><p><span>P${
                                j.priority} <span class="muted">·</span> ${
                                j.memory} MB</span><span>${j.burst - j.remaining}/${
                                j.burst} ticks</span></p><div class="job-progress"><span style="width:${
                                100 * (j.burst - j.remaining) / j.burst}%"></span></div>${
                                j.wait_reason ? `<div class="job-wait-reason">${
                                                    j.wait_reason === 'MEMORY_UNAVAILABLE'
                                                        ? 'Waiting for contiguous memory'
                                                        : 'Waiting for a build slot'}</div>`
                                              : ''}</article>`)
                        .join('') ||
                    '<div class="kanban-empty">No jobs here</div>'}${
                    jobs.length > 5 ? `<a href="#jobs" class="kanban-more">+${
                                          jobs.length - 5} more in job registry →</a>`
                                    : ''}</div>`;
            })
            .join('');
}
function renderLogs() {
    if (!state)
        return;
    const filter = $('log-filter').value;
    const events = state.log.filter(e => !filter || e.type.includes(filter)).slice(-250).reverse();
    $('event-log').innerHTML =
        events.length
            ? events
                  .map(e => `<div class="event-row ${
                           e.type.startsWith('MEMORY')     ? 'memory'
                           : e.type.startsWith('RESOURCE') ? 'resource'
                           : e.type.includes('ERROR')      ? 'error'
                                                      : ''}"><span>t=${e.tick}</span><time title="${
                           new Date(e.wall_epoch_ms).toISOString()}">${
                           new Date(e.wall_epoch_ms).toLocaleTimeString(undefined, {
                               hour12 : false
                           })}</time><span class="event-type">${
                           esc(e.type)}</span><span class="event-message">${
                           esc(e.message)}</span></div>`)
                  .join('')
            : empty('Quiet for now', 'Engine events appear here as the experiment progresses.');
}
function render() {
    if (!state)
        return;
    const s = state.stats, m = state.memory, r = state.resources, c = state.config;
    text('top-tick', String(s.tick).padStart(3, '0'));
    text('mode-badge', c.mode === 'simulation' ? 'SIMULATION' : 'LIVE EXECUTION');
    text('execution-scope', c.mode === 'simulation' ? 'DETERMINISTIC LOGICAL TICKS'
                                                    : 'LIVE WORK · LOGICAL ACCOUNTING');
    text('run-status', state.running ? 'RUNNING' : 'PAUSED');
    $('run-status').className = 'state-pill ' + (state.running ? 'state-running' : '');
    text('sidebar-status',
         `${c.workers} worker${c.workers > 1 ? 's' : ''} · ${c.policy.toUpperCase()}`);
    text('nav-job-count', s.total);
    const unfinished = s.total - s.completed - s.cancelled;
    const playback = state.running ? 'Experiment running'
                     : unfinished  ? 'Experiment paused'
                     : s.total     ? 'Experiment complete'
                                   : 'Ready when you are';
    text('playback-state', playback);
    text('playback-context',
         `${c.policy.toUpperCase()} · ${c.workers} logical worker${c.workers > 1 ? 's' : ''}`);
    $('run-btn').innerHTML =
        icon(state.running ? 'pause' : 'play') +
        (state.running ? ' Pause' : ' Run ' + (c.mode === 'live' ? 'execution' : 'simulation')) +
        ' <kbd>space</kbd>';
    $('run-btn').disabled = !unfinished;
    $('step-btn').disabled = state.running || !unfinished;
    text('experiment-name', state.scenario ? demoInfo[state.scenario]?.[0] || state.scenario
                            : s.total      ? 'Custom workload'
                                           : 'Your next experiment starts here');
    if (comparison && comparison.source_revision !== state.experiment_revision)
        invalidateComparison();
    text('experiment-detail',
         s.total
             ? `${s.total} jobs · ${c.policy.toUpperCase()} · ${fmt(c.memory)} MB simulated memory`
             : 'Load a reproducible workload or submit a job.');
    $('kpi-grid').innerHTML =
        kpi('Total jobs', fmt(s.total), '', 'In this experiment', 'jobs') +
        kpi('Running', fmt(s.running), '', `${c.workers} logical worker${c.workers > 1 ? 's' : ''}`,
            'cpu') +
        kpi('Completed', fmt(s.completed), '',
            s.total ? `${fmt(100 * s.completed / s.total)}% of all submissions`
                    : 'Awaiting first job',
            'layers') +
        kpi('Avg. waiting', fmt(s.avg_wait, 2), 'ticks', 'Completed jobs · READY only', 'logs') +
        kpi('CPU utilization', fmt(s.cpu_utilization, 1), '%',
            `${s.busy_worker_ticks} busy worker ticks`, 'chart') +
        kpi('Memory usage', fmt(m.utilization, 1), '%', `${fmt(m.used)} / ${fmt(m.total)} MB`,
            'memory');
    if (page === 'overview') {
        gantt($('overview-gantt'), state, 30);
        text('timeline-policy',
             c.policy.toUpperCase() + (c.policy === 'rr' ? ` · q${c.quantum}` : ''));
        text('timeline-elapsed', `${s.tick} elapsed ticks`);
        $('overview-memory-used').innerHTML = `${fmt(m.used)} <small>MB</small>`;
        text('overview-memory-capacity', `of ${fmt(m.total)} MB`);
        memoryMap($('overview-memory-map'), m);
        text('overview-largest', `${fmt(m.largest_free)} MB`);
        text('overview-fragmentation', `${fmt(m.external_fragmentation, 1)}%`);
        text('pipeline-count',
             `${s.total} jobs · ${state.jobs.filter(j => j.state === 'NEW').length} future · ${
                 s.cancelled} cancelled`);
        renderKanban();
        text('engine-explanation', state.explanation);
        $('quick-slots').innerHTML = `${r.active_slots} / ${r.slots} <small>occupied</small>`;
        $('quick-slot-dots').innerHTML =
            Array
                .from({length : Math.min(r.slots, 8)},
                      (_, i) => `<i class="${i < r.active_slots ? 'occupied' : ''}"></i>`)
                .join('');
    }
    if (page === 'jobs')
        renderJobs();
    if (page === 'scheduler') {
        gantt($('scheduler-gantt'), state, Number($('gantt-zoom').value));
        syncValue('policy-select', c.policy);
        syncValue('quantum-input', c.quantum);
        syncValue('delay-input', c.delay_ms);
        text('policy-description', policyDescriptions[c.policy]);
        text('scheduler-decision', state.explanation);
    }
    if (page === 'memory') {
        memoryMap($('full-memory-map'), m);
        text('memory-end-address', `${fmt(m.total)} MB`);
        syncValue('memory-policy-select', m.policy);
        $('memory-stats').innerHTML =
            [
                [ 'Allocated', m.used, 'MB' ], [ 'Free', m.free, 'MB' ],
                [ 'Largest hole', m.largest_free, 'MB' ],
                [ 'Free / allocated blocks', `${m.free_blocks} / ${m.allocated_blocks}`, '' ],
                [ 'External fragmentation', fmt(m.external_fragmentation, 1), '%' ],
                [ 'Internal fragmentation', m.internal_fragmentation, 'MB' ]
            ]
                .map(([ name, n, unit ]) =>
                         `<div><span>${name}</span><b>${n} <small>${unit}</small></b></div>`)
                .join('');
        $('memory-table').innerHTML =
            m.blocks
                .map(b => `<tr><td>${b.offset} MB</td><td>${b.offset + b.size} MB</td><td>${
                         b.size} MB</td><td>${
                         b.owner ? `<span class="job-id" style="color:${color(b.owner)}">Job ${
                                       b.owner}</span>`
                                 : 'Free'}</td></tr>`)
                .join('');
    }
    if (page === 'resources') {
        $('resource-kpis').innerHTML =
            kpi('Occupied slots', r.active_slots, `/ ${r.slots}`, 'Semaphore admission',
                'resources') +
            kpi('Blocked jobs', s.waiting, '', 'Waiting for memory or slots', 'jobs') +
            kpi('Contention events', s.contention_events, '', 'Denied admission attempts',
                'layers') +
            kpi('Shared counter', r.shared_counter, '', 'One atomic increment per tick', 'chart');
        const running = state.jobs.filter(j => j.state === 'RUNNING');
        $('resource-slots').innerHTML =
            Array
                .from({length : r.slots},
                      (_, i) =>
                          `<div class="resource-slot ${i < r.active_slots ? 'occupied' : ''}">${
                              icon('cpu')}Slot ${i + 1}<small>${
                              i < r.active_slots
                                  ? 'J' + running[i]?.id + ' · W' + (running[i]?.worker + 1)
                                  : 'Available'}</small></div>`)
                .join('');
        $('resource-waiters').innerHTML =
            state.jobs.filter(j => j.state === 'WAITING')
                .map(j => `<div class="waiter"><span>J${j.id} · ${esc(j.name)}</span><span>${
                         j.wait_reason === 'MEMORY_UNAVAILABLE'
                             ? 'Memory'
                             : 'Build slot'} · ${j.resource_wait} ticks</span></div>`)
                .join('') ||
            '<p class="panel-note">No jobs blocked on resources.</p>';
        syncValue('workers-input', c.workers);
        syncValue('slots-input', c.slots);
        syncValue('mode-select', c.mode);
    }
    const invariant =
        r.active_slots >= 0 && r.active_slots <= r.slots && r.active_slots === s.running;
    text('invariant-status', invariant ? '✓ maintained' : '✕ inconsistent state');
    $('invariant-status').classList.toggle('form-error', !invariant);
    if (page === 'logs')
        renderLogs();
    updateControls();
}
async function refresh(force = false) {
    if (mutations.busy && !force)
        return;
    if (refreshPromise)
        return refreshPromise;
    const epoch = requestEpoch;
    refreshPromise = (async () => {
        try {
            const next = await api('snapshot');
            if (epoch !== requestEpoch)
                return;
            state = next;
            online = true;
            const loggingError = state.logging?.error;
            $('connection-error').hidden = !loggingError;
            if (loggingError)
                text(
                    'connection-error',
                    loggingError +
                        '. Latest events remain in memory. Restore disk access and restart to resume persistent logging.');
            render();
        } catch (e) {
            if (epoch !== requestEpoch)
                return;
            online = false;
            invalidateComparison();
            $('connection-error').hidden = false;
            text('connection-error', `Engine connection unavailable. ${
                                         e.message}. Start ./buildqueue --serve --port ${location.port || 8080}.`);
            text('sidebar-status', 'Disconnected');
            updateControls();
        } finally {
            refreshPromise = null;
        }
    })();
    return refreshPromise;
}
async function action(path, body = {}, message) {
    try {
        return await mutations.run(async () => {
            ++requestEpoch;
            const result = await api(path, body);
            if (path === 'reset' || path === 'load-demo')
                clearDrafts();
            if (refreshPromise)
                await refreshPromise;
            await refresh(true);
            if (message)
                toast(message);
            return result;
        });
    } catch (e) {
        toast(e.message, true);
        throw e;
    }
}
function bindForm(id, fn) {
    $(id).addEventListener('input', () => { $(id).dataset.dirty = 'true'; });
    $(id).addEventListener('submit', async event => {
        event.preventDefault();
        if (mutations.busy)
            return;
        try {
            await fn();
            delete $(id).dataset.dirty;
        } catch {
        } finally {
            if (state)
                render();
        }
    });
}
function invalidateComparison() {
    comparison = null;
    $('comparison-content').className = 'panel empty-state';
    $('comparison-content').innerHTML =
        empty('Measure the trade-offs', 'Load a workload, then compare all four policies.');
    $('comparison-exports').hidden = true;
}
async function loadDemo(name) {
    await action('load-demo', {name}, 'Demo loaded. Step through the experiment or press Run.');
    invalidateComparison();
    $('demo-dialog').close();
    render();
}
mountIcons();
let theme;
try {
    theme = localStorage.getItem('buildqueue-theme');
} catch {
}
document.documentElement.dataset.theme = theme === 'light' ? 'light' : 'dark';
$('theme-toggle').addEventListener('click', () => {
    const value = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = value;
    updateControls();
    try {
        localStorage.setItem('buildqueue-theme', value);
    } catch {
    }
});
window.addEventListener('hashchange', go);
go();
updateControls();
$('new-job-btn').addEventListener('click', () => {
    if (state) {
        $('job-form').elements.arrival.value = state.stats.tick;
        $('job-form').elements.arrival.min = state.stats.tick;
        $('job-form').elements.arrival.dataset.arrivalNow = 'true';
        $('job-form').elements.memory.max = state.memory.total;
    }
    $('job-form-error').textContent = '';
    $('job-dialog').showModal();
});
$('demo-btn').addEventListener('click', () => $('demo-dialog').showModal());
$('job-form')
    .elements.arrival.addEventListener('input',
                                       event => { delete event.target.dataset.arrivalNow; });
document.addEventListener('click', event => {
    const close = event.target.closest('[data-close]');
    if (close)
        $(close.dataset.close).close();
    const cancel = event.target.closest('[data-cancel]');
    if (cancel)
        action('cancel', {id : Number(cancel.dataset.cancel)}, 'Job cancelled; resources released.')
            .catch(() => {});
    const demo = event.target.closest('[data-demo]');
    if (demo)
        loadDemo(demo.dataset.demo).catch(() => {});
});
bindForm('job-form', async () => {
    const form = $('job-form');
    const data = Object.fromEntries(new FormData(form));
    for (const k of ['arrival', 'burst', 'priority', 'memory'])
        data[k] = Number(data[k]);
    if (form.elements.arrival.dataset.arrivalNow === 'true') {
        data.arrival_now = true;
        delete data.arrival;
    }
    try {
        const result = await action('submit', data);
        form.reset();
        $('job-dialog').close();
        invalidateComparison();
        toast(`Job J${result.id} submitted.`);
    } catch (e) {
        text('job-form-error', e.message);
        throw e;
    }
});
bindForm('policy-form', async () => {
    await action('policy',
                 {policy : $('policy-select').value, quantum : Number($('quantum-input').value)},
                 'Scheduling policy updated.');
    invalidateComparison();
});
bindForm('memory-policy-form', async () => {
    await action('memory-policy', {policy : $('memory-policy-select').value},
                 'Memory policy updated.');
    invalidateComparison();
});
bindForm('delay-form', () => action('config', {delay_ms : Number($('delay-input').value)},
                                    'Playback delay updated.'));
bindForm('resource-form', async () => {
    await action('config', {
        workers : Number($('workers-input').value),
        slots : Number($('slots-input').value),
        mode : $('mode-select').value
    },
                 'Resource configuration updated.');
    invalidateComparison();
});
$('run-btn').addEventListener('click',
                              () => action(state?.running ? 'pause' : 'run').catch(() => {}));
$('step-btn').addEventListener('click', () => action('step').catch(() => {}));
$('reset-btn').addEventListener('click', async () => {
    try {
        await action('reset', {}, 'Experiment reset. Configuration preserved.');
        invalidateComparison();
        render();
    } catch {
    }
});
document.addEventListener('keydown', event => {
    if (event.target.closest('input,select,textarea,button') ||
        document.querySelector('dialog[open]'))
        return;
    if (event.code === 'Space') {
        event.preventDefault();
        $('run-btn').click();
    } else if (event.key === '.')
        $('step-btn').click();
});
for (const id of ['job-search', 'job-filter', 'job-sort'])
    $(id).addEventListener('input', renderJobs);
$('log-filter').addEventListener('change', renderLogs);
$('gantt-zoom').addEventListener('input', () => {
    if (state)
        gantt($('scheduler-gantt'), state, Number($('gantt-zoom').value));
});
$('compare-btn').addEventListener('click', async () => {
    const button = $('compare-btn');
    const previous = button.innerHTML;
    button.textContent = 'Replaying four policies…';
    try {
        const result = await action('compare', {});
        if (!online || !state || result.source_revision !== state.experiment_revision)
            throw new Error('The workload changed during comparison. Run comparison again.');
        comparison = result;
        $('comparison-content').className = 'panel';
        $('comparison-content').innerHTML = comparisonCharts(comparison);
        $('comparison-exports').hidden = false;
        toast('Four isolated runs completed.');
    } catch (e) {
        toast(e.message, true);
    } finally {
        button.innerHTML = previous;
        updateControls();
    }
});
$('export-comparison-json').addEventListener('click', () => {
    if (comparison)
        download('buildqueue-comparison.json', JSON.stringify(comparison, null, 2));
});
$('export-comparison-csv').addEventListener('click', () => {
    if (!comparison)
        return;
    download('buildqueue-comparison.csv', comparisonCsv(comparison), 'text/csv');
});
$('export-log').addEventListener('click', () => {
    if (state)
        download('buildqueue-events.json', JSON.stringify(state.log, null, 2));
});
$('clear-log')
    .addEventListener(
        'click', () => action('clear-log', {},
                              'In-memory event view cleared. Persistent execution log retained.')
                           .catch(() => {}));
try {
    const demos = await api('demos');
    $('demo-list').innerHTML =
        demos
            .map((name, i) => `<button class="demo-card" data-demo="${esc(name)}"><span>${
                     icon(i < 4   ? 'cpu'
                          : i < 9 ? 'memory'
                                  : 'resources')}</span><div><b>${
                     esc(demoInfo[name]?.[0] ||
                         name)}</b><p>${esc(demoInfo[name]?.[1] || '')}</p></div></button>`)
            .join('');
} catch (e) {
    $('demo-list').innerHTML = empty('Cannot load scenarios', e.message);
}
updateControls();
await refresh();
setInterval(refresh, 500);
