export const gameConfigurationResponse = Object.freeze({
  height: 600,
  interval: 16,
  part_cols: 4,
  part_rows: 3,
  radius: 10,
  width: 800,
});

export const expectedGameConfiguration = new Map([
  ...Object.entries(gameConfigurationResponse),
  ['part_height', 200],
  ['part_width', 200],
]);
