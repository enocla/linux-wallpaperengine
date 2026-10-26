globalThis.__intervals = Object.create(null);
globalThis.localStorage = globalThis.localStorage || {
  __data: Object.create(null),
  get(key) {
    key = String(key);
    return Object.prototype.hasOwnProperty.call(this.__data, key) ? this.__data[key] : null;
  },
  set(key, value) { this.__data[String(key)] = String(value); },
  remove(key) { delete this.__data[String(key)]; },
  clear() { this.__data = Object.create(null); }
};
globalThis.MediaPlaybackEvent = globalThis.MediaPlaybackEvent || {
  PLAYBACK_STOPPED: 0,
  PLAYBACK_PLAYING: 1,
  PLAYBACK_PAUSED: 2
};

// createScriptProperties() builder. The values defined in the scene file (exposed by the engine through
// globalThis.__wpeScriptProperties while a script module is being evaluated) take precedence over the defaults
// declared by the script itself.
globalThis.createScriptProperties = function () {
  const live = globalThis.__wpeScriptProperties;
  const defaults = Object.create(null);
  const builder = {};
  const add = function (options) {
    if (options && options.name !== undefined) {
      defaults[options.name] = options.value;
    }
    return builder;
  };

  for (const method of ['addSlider', 'addCheckbox', 'addCombo', 'addColor', 'addText', 'addTextInput', 'addFile', 'addDirectory']) {
    builder[method] = add;
  }

  builder.finish = function () {
    return new Proxy(defaults, {
      get(target, key) {
        if (live && typeof key === 'string') {
          const value = live[key];
          if (value !== undefined) {
            return value;
          }
        }
        return target[key];
      },
      set(target, key, value) {
        target[key] = value;
        return true;
      },
      has(target, key) {
        return key in target || (live !== undefined && typeof key === 'string' && live[key] !== undefined);
      }
    });
  };

  return builder;
};

// Minimal stand-in for animation objects returned by ILayer.getAnimation () and similar helpers
globalThis.__wpeAnimationStub = {
  play() {}, stop() {}, pause() {}, isPlaying() { return false; },
  getFrame() { return 0; }, setFrame() {}, frameCount: 0, duration: 0, rate: 1
};

// Prototype for ILayer instances, the engine resolves layer properties natively and falls back to these helpers
globalThis.__wpeLayerPrototype = {
  getAnimation() { return globalThis.__wpeAnimationStub; },
  getTextureAnimation() { return globalThis.__wpeAnimationStub; },
  getEffect() { return undefined; },
  getEffectCount() { return 0; },
  getParent() { return null; },
  getChildren() { return []; },
  getBoneCount() { return 0; },
  getAnimationLayerCount() { return 0; },
  getAnimationLayer() { return globalThis.__wpeAnimationStub; },
  play() {}, stop() {}, pause() {}, isPlaying() { return false; }
};
