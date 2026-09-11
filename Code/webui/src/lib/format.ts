// Number formatting shared by the pages. One decimal for temperatures
// everywhere, so columns and cards line up.
export const fmtTemp = (value: number): string => Number(value).toFixed(1);

// Setpoints move in half-degree steps between 5 and 30 C (the range the
// thermostats accept over TrOverride).
export const SETPOINT_MIN = 5;
export const SETPOINT_MAX = 30;
export const SETPOINT_STEP = 0.5;
export const clampTemp = (value: number): number =>
  Math.min(SETPOINT_MAX, Math.max(SETPOINT_MIN, Math.round(value / SETPOINT_STEP) * SETPOINT_STEP));

export function fmtBytes(bytes: number): string {
  if (bytes >= 1048576) {
    const mb = bytes / 1048576;
    return `${Number.isInteger(mb) ? mb : mb.toFixed(1)} MB`;
  }
  if (bytes >= 1024) return `${Math.round(bytes / 1024)} KB`;
  return `${bytes} B`;
}
