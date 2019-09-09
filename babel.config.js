module.exports = (api) => {
  api.cache(true);
  return {
    presets: [
      ['@babel/preset-env', { targets: 'current node' }],
    ],
  };
};
