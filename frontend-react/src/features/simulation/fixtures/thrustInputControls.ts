export interface ThrustInputControls {
  readonly container: HTMLDivElement;
  readonly number: HTMLInputElement;
  readonly range: HTMLInputElement;
  readonly apply: HTMLButtonElement;
  readonly input: HTMLInputElement;
  readonly textarea: HTMLTextAreaElement;
  readonly select: HTMLSelectElement;
  readonly editable: HTMLDivElement;
  readonly editableChild: HTMLSpanElement;
  readonly camera: HTMLButtonElement;
}

/** Explicit tuning controls and native editors, beside an intentionally unmarked camera button. */
export function createThrustInputControls(): ThrustInputControls {
  const container = document.createElement('div');
  const tuningRegion = document.createElement('section');
  tuningRegion.dataset.gameplayInput = 'blocked';
  const number = document.createElement('input');
  number.type = 'number';
  const range = document.createElement('input');
  range.type = 'range';
  const apply = document.createElement('button');
  apply.textContent = 'Apply tuning';
  tuningRegion.append(number, range, apply);

  const input = document.createElement('input');
  const textarea = document.createElement('textarea');
  const select = document.createElement('select');
  const editable = document.createElement('div');
  editable.setAttribute('contenteditable', 'true');
  editable.tabIndex = 0;
  const editableChild = document.createElement('span');
  editableChild.textContent = 'Editable draft';
  editable.append(editableChild);
  const camera = document.createElement('button');
  camera.textContent = 'Follow player';
  container.append(tuningRegion, input, textarea, select, editable, camera);
  document.body.append(container);

  return {
    container,
    number,
    range,
    apply,
    input,
    textarea,
    select,
    editable,
    editableChild,
    camera,
  };
}
