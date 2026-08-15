/* Small shared building blocks: cards, pills, empty states, confirm, toast.
 * Kept in one file — each is a few lines, and the design system is Tailwind
 * classes, not a component library. */

import {
  Component,
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
  type ErrorInfo,
  type MouseEvent as ReactMouseEvent,
  type FocusEvent as ReactFocusEvent,
  type ReactNode,
} from "react";
import { CLASS_COLOR, CLASS_NAME, CLASS_TEXT_COLOR } from "../data/wow";

/* ---- form fields ---- */

/* Every field in the app is the same box; spelling it out per input is how the
 * launch form drifted into four slightly different borders. */
export const FIELD =
  "w-full rounded-lg border border-ink-700 bg-ink-950/70 px-3 py-2 text-sm text-ink-100 " +
  "outline-none transition focus:border-iris-400/70 focus:bg-ink-950 " +
  "placeholder:text-ink-600";
export const FIELD_BAD = "border-rose-500/70 focus:border-rose-400";
/* Same box, with room kept clear for the chevron index.css draws. */
export const SELECT = `${FIELD} pr-9`;

/* Tab into a box and the value is selected, so you overtype instead of
 * backspacing — the whole point of these being small numeric boxes.
 *
 * onFocus+select alone loses the selection on a *click*, because the browser
 * places the caret on the following mouseup. We swallow that mouseup, but only
 * the one that arrives with the focus — once the field is already focused,
 * clicking and drag-selecting inside it behave normally. */
let armedTarget: EventTarget | null = null;
export const selectOnFocus = {
  onMouseDown(e: ReactMouseEvent<HTMLInputElement>) {
    armedTarget =
      document.activeElement === e.currentTarget ? null : e.currentTarget;
  },
  onFocus(e: ReactFocusEvent<HTMLInputElement>) {
    e.currentTarget.select();
  },
  onMouseUp(e: ReactMouseEvent<HTMLInputElement>) {
    if (armedTarget === e.currentTarget) {
      e.preventDefault();
      armedTarget = null;
    }
  },
};

export function Field({
  label,
  hint,
  children,
  className = "",
}: {
  label: ReactNode;
  hint?: ReactNode;
  children: ReactNode;
  className?: string;
}) {
  return (
    <label className={`block ${className}`}>
      <span className="flex items-baseline gap-1.5 text-xs font-medium uppercase tracking-wide text-ink-400">
        {label}
        {hint && (
          <span className="font-normal normal-case tracking-normal text-ink-600">
            {hint}
          </span>
        )}
      </span>
      <div className="mt-1.5">{children}</div>
    </label>
  );
}

/* A digits-only box held as TEXT, not <input type="number">: a number input
 * snaps a cleared box back to 0, hides the value behind spinners, and scrolls
 * itself when the wheel passes over. */
export function NumberBox({
  value,
  onChange,
  invalid,
  placeholder,
}: {
  value: string;
  onChange: (v: string) => void;
  invalid?: boolean;
  placeholder?: string;
}) {
  return (
    <input
      type="text"
      inputMode="numeric"
      autoComplete="off"
      spellCheck={false}
      placeholder={placeholder}
      value={value}
      onChange={(e) => onChange(e.target.value.replace(/[^\d]/g, ""))}
      {...selectOnFocus}
      className={`${FIELD} ${invalid ? FIELD_BAD : ""}`}
    />
  );
}

/* ---- modal plumbing ---- */

const FOCUSABLE =
  'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), ' +
  'textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

/* Attach to the panel of anything with role="dialog".
 *
 * A dialog that only LOOKS modal is the accessibility hole: the panel paints
 * over the page, but focus is still on the button that opened it, so Tab walks
 * the dungeon grid behind the overlay and a screen reader never enters the
 * form. This moves focus in, keeps it in, hands it back on close, and stops the
 * page behind from scrolling under the overlay. */
export function useModal<T extends HTMLElement>(onClose: () => void) {
  const ref = useRef<T>(null);
  /* Through a ref, so the effect below runs once per OPEN. Depending on the
     callback would re-run it whenever the caller re-rendered, which yanks
     focus back to the first field mid-typing. */
  const close = useRef(onClose);
  close.current = onClose;

  useEffect(() => {
    const opener = document.activeElement as HTMLElement | null;
    const panel = ref.current;
    /* The panel itself (give it tabIndex={-1}), or whatever the caller marked
       with data-autofocus. Deliberately NOT "the first focusable child": in
       document order that is the ✕, so opening a launch form would arm Enter
       to CLOSE it — the opposite of what a tester pressing Enter wants. From
       the panel, a screen reader reads the dialog's label and the first Tab
       goes where the markup says. */
    const pick = panel?.querySelector<HTMLElement>("[data-autofocus]");
    (pick ?? panel)?.focus();

    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.stopPropagation();
        close.current();
        return;
      }
      if (e.key !== "Tab" || !panel) return;
      const items = [...panel.querySelectorAll<HTMLElement>(FOCUSABLE)].filter(
        (el) => el.offsetParent !== null,
      );
      if (!items.length) return;
      const edge = e.shiftKey ? items[0] : items[items.length - 1];
      if (document.activeElement === edge) {
        e.preventDefault();
        (e.shiftKey ? items[items.length - 1] : items[0]).focus();
      }
    };

    const scroll = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    window.addEventListener("keydown", onKey, true);
    return () => {
      window.removeEventListener("keydown", onKey, true);
      document.body.style.overflow = scroll;
      opener?.focus?.();
    };
  }, []);

  return ref;
}

/* ---- disclosure ---- */

/* The chevron on an expandable history row, and the real control behind it.
 *
 * The row itself stays a click target for the mouse, but it cannot BE the
 * button: it already contains rerun/copy buttons, and nesting those inside a
 * <button> is invalid markup that browsers resolve unpredictably. So the
 * chevron carries the semantics — focusable, labelled, and reporting its own
 * expanded state — and keyboard users have one thing to hit per row. */
export function ExpandToggle({
  expanded,
  onToggle,
  label,
}: {
  expanded: boolean;
  onToggle: () => void;
  label: string;
}) {
  return (
    <button
      type="button"
      aria-expanded={expanded}
      aria-label={`${expanded ? "Hide" : "Show"} details for ${label}`}
      onClick={(e) => {
        e.stopPropagation();
        onToggle();
      }}
      className="rounded px-1 text-ink-600 transition hover:text-ink-300"
    >
      {expanded ? "▾" : "▸"}
    </button>
  );
}

export function Card({
  children,
  className = "",
}: {
  children: ReactNode;
  className?: string;
}) {
  return (
    <div
      className={`rounded-2xl border border-ink-800 bg-ink-900/60 p-5 shadow-lg shadow-ink-950/40 ${className}`}
    >
      {children}
    </div>
  );
}

export function CardTitle({
  children,
  right,
}: {
  children: ReactNode;
  right?: ReactNode;
}) {
  return (
    <div className="mb-4 flex items-center justify-between gap-3">
      <h2 className="text-sm font-semibold uppercase tracking-wider text-ink-400">
        {children}
      </h2>
      {right}
    </div>
  );
}

export function ResultPill({ result }: { result?: string }) {
  const r = (result ?? "").toLowerCase();
  const style = r.includes("success")
    ? "bg-emerald-500/15 text-emerald-300 border-emerald-700/50"
    : r.includes("wipe") || r.includes("fail")
      ? "bg-red-500/15 text-red-300 border-red-800/50"
      : r.includes("stopped") || r.includes("abort")
        ? "bg-ink-500/15 text-ink-300 border-ink-700"
        : "bg-amber-500/15 text-amber-300 border-amber-800/50";
  return (
    <span
      className={`inline-block rounded-full border px-2.5 py-0.5 text-xs font-medium ${style}`}
    >
      {result || "?"}
    </span>
  );
}

export function ClassChip({
  classId,
  name,
  dead,
}: {
  classId?: number;
  name?: string;
  dead?: boolean;
}) {
  const color = classId ? CLASS_COLOR[classId] : "#71717a";
  const text = classId ? CLASS_TEXT_COLOR[classId] : "#a1a1aa";
  return (
    <span
      className={`inline-flex items-center gap-1.5 rounded-full border border-ink-700/70 bg-ink-900 px-2 py-0.5 text-xs ${dead ? "opacity-40 line-through" : ""}`}
      title={classId ? CLASS_NAME[classId] : undefined}
    >
      <span
        className="h-2 w-2 rounded-full"
        style={{ backgroundColor: color }}
      />
      <span style={{ color: text }}>{name ?? CLASS_NAME[classId ?? 0] ?? "?"}</span>
    </span>
  );
}

export function HpBar({ hp, mp }: { hp?: number; mp?: number }) {
  const h = Math.max(0, Math.min(100, hp ?? 0));
  return (
    <span className="inline-flex w-16 flex-col gap-0.5 align-middle">
      <span className="h-1.5 overflow-hidden rounded bg-ink-800">
        <span
          className={`block h-full ${h > 50 ? "bg-emerald-500" : h > 20 ? "bg-amber-500" : "bg-red-500"}`}
          style={{ width: `${h}%` }}
        />
      </span>
      {mp !== undefined && mp >= 0 && (
        <span className="h-1 overflow-hidden rounded bg-ink-800">
          <span
            className="block h-full bg-sky-500"
            style={{ width: `${Math.max(0, Math.min(100, mp))}%` }}
          />
        </span>
      )}
    </span>
  );
}

/* Copy-to-clipboard for run/plan ids — these get pasted into `.dc test stop`,
 * chat and bug reports constantly.
 *
 * navigator.clipboard only exists in SECURE contexts — and a LAN deployment
 * is plain http://<ip>:8790, where Safari/Chrome expose no clipboard API at
 * all (localhost is exempt, which is why it "works on one machine"). The
 * hidden-textarea execCommand path is the fallback that works everywhere. */
async function copyText(text: string): Promise<boolean> {
  if (navigator.clipboard && window.isSecureContext) {
    try {
      await navigator.clipboard.writeText(text);
      return true;
    } catch {
      /* fall through to the legacy path */
    }
  }
  const ta = document.createElement("textarea");
  ta.value = text;
  ta.setAttribute("readonly", "");
  ta.style.position = "fixed";
  ta.style.opacity = "0";
  document.body.appendChild(ta);
  ta.select();
  ta.setSelectionRange(0, text.length);   // iOS Safari needs the range
  let ok = false;
  try {
    ok = document.execCommand("copy");
  } catch {
    ok = false;
  }
  document.body.removeChild(ta);
  return ok;
}

export function CopyButton({ text, title }: { text?: string; title?: string }) {
  const [copied, setCopied] = useState(false);
  if (!text) return null;
  return (
    <button
      title={title ?? `copy ${text}`}
      onClick={(e) => {
        e.stopPropagation();
        void copyText(text).then((ok) => {
          if (!ok) return;
          setCopied(true);
          setTimeout(() => setCopied(false), 1200);
        });
      }}
      className={`rounded border px-1.5 py-0.5 text-[10px] transition ${
        copied
          ? "border-emerald-700 text-emerald-300"
          : "border-ink-700 text-ink-500 hover:border-ink-500 hover:text-ink-300"
      }`}
    >
      {copied ? "copied" : "copy"}
    </button>
  );
}

export function EmptyState({
  icon,
  title,
  children,
}: {
  icon: string;
  title: string;
  children?: ReactNode;
}) {
  return (
    <div className="flex flex-col items-center gap-2 rounded-2xl border border-dashed border-ink-800 py-14 text-center">
      <div className="text-3xl opacity-60">{icon}</div>
      <div className="font-medium text-ink-300">{title}</div>
      {children && <div className="max-w-md text-sm text-ink-500">{children}</div>}
    </div>
  );
}

export function Spinner({ label }: { label?: string }) {
  return (
    <span className="inline-flex items-center gap-2 text-sm text-ink-400">
      <span className="h-3.5 w-3.5 animate-spin rounded-full border-2 border-ink-600 border-t-iris-400" />
      {label}
    </span>
  );
}

/* ---- confirm dialog ---- */

export function ConfirmButton({
  label,
  confirmLabel,
  message,
  onConfirm,
  className = "",
  tone = "danger",
}: {
  label: ReactNode;
  confirmLabel?: string;
  message: string;
  onConfirm: () => void;
  className?: string;
  /* Most of these stop something; a launch confirm is not a red button. */
  tone?: "danger" | "go";
}) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <button
        className={className}
        onClick={(e) => {
          /* These live inside click-to-expand rows on History. */
          e.stopPropagation();
          setOpen(true);
        }}
      >
        {label}
      </button>
      {open && (
        <ConfirmDialog
          message={message}
          confirmLabel={confirmLabel}
          tone={tone}
          onCancel={() => setOpen(false)}
          onConfirm={() => {
            setOpen(false);
            onConfirm();
          }}
        />
      )}
    </>
  );
}

/* Split out so useModal is only ever mounted with the dialog — a hook cannot
 * be called conditionally, and this dialog exists conditionally. */
function ConfirmDialog({
  message,
  confirmLabel,
  tone,
  onCancel,
  onConfirm,
}: {
  message: string;
  confirmLabel?: string;
  tone: "danger" | "go";
  onCancel: () => void;
  onConfirm: () => void;
}) {
  const panel = useModal<HTMLDivElement>(onCancel);
  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4"
      onClick={(e) => {
        e.stopPropagation();
        onCancel();
      }}
    >
      <div
        ref={panel}
        role="alertdialog"
        aria-modal="true"
        aria-label={message}
        tabIndex={-1}
        className="w-full max-w-sm rounded-2xl border border-ink-700 bg-ink-900 p-6"
        onClick={(e) => e.stopPropagation()}
      >
        <p className="text-sm text-ink-300">{message}</p>
        <div className="mt-5 flex justify-end gap-3">
          {/* Cancel takes the focus: most of these stop a run or wipe shared
              history, and a stray Enter on a dialog that just appeared should
              land on the harmless half. */}
          <button
            data-autofocus
            className="rounded-lg px-3 py-1.5 text-sm text-ink-400 hover:text-ink-200"
            onClick={onCancel}
          >
            Cancel
          </button>
          <button
            className={`rounded-lg px-3 py-1.5 text-sm font-medium ${
              tone === "go"
                ? "bg-iris-600 text-white hover:bg-iris-500"
                : "bg-red-600/90 text-white hover:bg-red-500"
            }`}
            onClick={onConfirm}
          >
            {confirmLabel ?? "Confirm"}
          </button>
        </div>
      </div>
    </div>
  );
}

/* ---- toasts ---- */

interface Toast {
  id: number;
  kind: "ok" | "error";
  text: string;
}

const ToastCtx = createContext<(kind: Toast["kind"], text: string) => void>(
  () => {},
);

export function useToast() {
  return useContext(ToastCtx);
}

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<Toast[]>([]);
  const push = useCallback((kind: Toast["kind"], text: string) => {
    const id = Date.now() + Math.random();
    setToasts((t) => [...t, { id, kind, text }]);
    setTimeout(
      () => setToasts((t) => t.filter((x) => x.id !== id)),
      kind === "error" ? 8000 : 4000,
    );
  }, []);
  return (
    <ToastCtx.Provider value={push}>
      {children}
      {/* "Run started" and every launch refusal arrive only as a toast, so the
          region has to be announced — otherwise a screen reader gets silence
          where the sighted user gets the entire outcome of the action. */}
      <div
        role="status"
        aria-live="polite"
        aria-atomic="false"
        className="pointer-events-none fixed bottom-16 right-4 z-50 flex w-80 flex-col gap-2 lg:bottom-4"
      >
        {toasts.map((t) => (
          <div
            key={t.id}
            role={t.kind === "error" ? "alert" : undefined}
            className={`pointer-events-auto rounded-xl border px-4 py-3 text-sm shadow-lg backdrop-blur ${
              t.kind === "ok"
                ? "border-emerald-800/60 bg-emerald-950/80 text-emerald-200"
                : "border-red-800/60 bg-red-950/80 text-red-200"
            }`}
          >
            {t.text}
          </div>
        ))}
      </div>
    </ToastCtx.Provider>
  );
}

/* ---- error boundary ---- */

/* Without this, one render throw blanks the whole app to an empty dark page
 * and the tester's bug report is "it went white". That is not a hypothetical
 * here: the history records are an ADDITIVE schema read straight out of JSONL,
 * so a row written by a newer module than the dist/ bundle is exactly the
 * shape that reaches a component with a field it does not expect.
 *
 * The message is shown verbatim because the audience is the person who can
 * fix it, and "Reload" is offered because the SPA holds no unsaved state. */
export class ErrorBoundary extends Component<
  { children: ReactNode },
  { error: Error | null }
> {
  state: { error: Error | null } = { error: null };

  static getDerivedStateFromError(error: Error) {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error("DC Test Deck crashed:", error, info.componentStack);
  }

  render() {
    if (!this.state.error) return this.props.children;
    return (
      <div className="flex min-h-dvh items-center justify-center p-4">
        <div className="w-full max-w-lg rounded-2xl border border-red-900/60 bg-ink-900/80 p-6">
          <div className="text-2xl">💥</div>
          <h1 className="mt-2 text-lg font-semibold text-ink-100">
            The page hit an error
          </h1>
          <p className="mt-1 text-sm text-ink-400">
            Nothing on the server was affected — runs already going keep going.
          </p>
          <pre className="mt-4 max-h-48 overflow-auto rounded-lg border border-ink-800 bg-ink-950 p-3 font-mono text-xs text-red-300">
            {this.state.error.message}
          </pre>
          <div className="mt-4 flex gap-3">
            <button
              onClick={() => window.location.reload()}
              className="rounded-lg bg-iris-600 px-4 py-2 text-sm font-medium text-white hover:bg-iris-500"
            >
              Reload
            </button>
            <button
              onClick={() => this.setState({ error: null })}
              className="rounded-lg border border-ink-700 px-4 py-2 text-sm text-ink-300 hover:border-ink-500"
            >
              Try again
            </button>
          </div>
        </div>
      </div>
    );
  }
}
