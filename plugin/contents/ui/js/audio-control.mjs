// Install before wallpaper scripts run. Event-driven: no timers or polling.
// Chromium has no per-view audio-track disable API. Suspend Web Audio and pause
// audio-only elements; keep audioMuted as a fallback for video/unsupported APIs.
export function script(disabled) {
    return `(() => {
        if (window.__wekdeSetAudioDisabled) return;
        let disabled = ${Boolean(disabled)};
        const contexts = new Map();
        const pausedAudio = new Set();
        const tracks = new Map();
        const nativePlay = HTMLMediaElement.prototype.play;
        const nativePause = HTMLMediaElement.prototype.pause;
        const ignore = result => { if (result && result.catch) result.catch(() => {}); };

        for (const name of ['AudioContext', 'webkitAudioContext']) {
            const Original = window[name];
            if (!Original) continue;
            window[name] = class extends Original {
                constructor(...args) {
                    super(...args);
                    contexts.set(this, disabled && this.state === 'running');
                    if (disabled) ignore(super.suspend());
                }
                resume() {
                    if (disabled) { contexts.set(this, true); return Promise.resolve(); }
                    return super.resume();
                }
                suspend() { contexts.set(this, false); return super.suspend(); }
                close() { contexts.delete(this); return super.close(); }
                __wekdeSuspend() { return super.suspend(); }
            };
        }
        HTMLMediaElement.prototype.play = function(...args) {
            if (disabled && this.tagName === 'AUDIO') {
                pausedAudio.add(this);
                return Promise.resolve();
            }
            return nativePlay.apply(this, args);
        };
        HTMLMediaElement.prototype.pause = function(...args) {
            pausedAudio.delete(this);
            return nativePause.apply(this, args);
        };
        function stopAudio(element) {
            if (element.tagName === 'AUDIO' && !element.paused) {
                pausedAudio.add(element);
                nativePause.call(element);
            }
            if (element.audioTracks) {
                for (const track of element.audioTracks) {
                    if (!tracks.has(track)) tracks.set(track, track.enabled);
                    track.enabled = false;
                }
            }
        }
        window.__wekdeSetAudioDisabled = value => {
            disabled = Boolean(value);
            for (const [context, resume] of contexts) {
                if (disabled) {
                    contexts.set(context, resume || context.state === 'running');
                    ignore(context.__wekdeSuspend());
                } else if (resume && context.state !== 'closed') {
                    contexts.set(context, false);
                    ignore(context.resume());
                }
            }
            if (disabled) document.querySelectorAll('audio,video').forEach(stopAudio);
            else {
                for (const [track, enabled] of tracks) track.enabled = enabled;
                tracks.clear();
                for (const element of pausedAudio) ignore(nativePlay.call(element));
                pausedAudio.clear();
            }
            // Apply the same setting to injected controllers in child frames.
            for (let i = 0; i < window.frames.length; i++)
                window.frames[i].postMessage({wekdeAudioDisabled: disabled}, '*');
        };
        window.addEventListener('message', event => {
            if (event.source === window.parent && window.parent !== window
                && typeof event.data?.wekdeAudioDisabled === 'boolean')
                window.__wekdeSetAudioDisabled(event.data.wekdeAudioDisabled);
        });
        document.addEventListener('play', event => {
            if (disabled) stopAudio(event.target);
        }, true);
        document.addEventListener('loadedmetadata', event => {
            if (disabled) stopAudio(event.target);
        }, true);
    })();`;
}
