// Export exactly the authoritative metrics used by the comparison table.
export const comparisonFields = [
    'avg_wait', 'avg_turnaround', 'avg_response', 'cpu_utilization', 'throughput',
    'context_switch_like_transitions'
];
export function comparisonCsv(comparison) {
    return [
        'policy,' + comparisonFields.join(','),
        ...comparison.results.map(
            row => [row.policy, ...comparisonFields.map(key => row.stats[key])].join(','))
    ].join('\n') +
           '\n';
}
