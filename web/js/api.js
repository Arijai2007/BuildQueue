export async function api(path, body) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), path === 'compare' ? 120000 : 15000);
    try {
        const response = await fetch(`/api/${path}`, {
            method : body === undefined ? 'GET' : 'POST',
            headers : body === undefined ? {} : {'Content-Type' : 'application/json'},
            body : body === undefined ? undefined : JSON.stringify(body),
            signal : controller.signal
        });
        let result;
        try {
            result = await response.json();
        } catch (error) {
            if (error.name === 'AbortError')
                throw error;
            throw new Error(`Engine returned HTTP ${response.status} with invalid JSON`);
        }
        if (!response.ok || !result.ok)
            throw new Error(result.error || `HTTP ${response.status}`);
        return result.data;
    } catch (error) {
        throw new Error(error.name === 'AbortError'
                            ? 'The engine request timed out. Refresh state before retrying.'
                            : error.message);
    } finally {
        clearTimeout(timeout);
    }
}
export function createMutationGate(onChange = () => {}) {
    let busy = false;
    return {
        get busy() {
            return busy;
        },
        async run(task) {
            if (busy)
                throw new Error('An engine operation is already in progress.');
            busy = true;
            onChange();
            try {
                return await task();
            } finally {
                busy = false;
                onChange();
            }
        }
    };
}
export function download(name, content, type = 'application/json') {
    const url = URL.createObjectURL(new Blob([ content ], {type}));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}
