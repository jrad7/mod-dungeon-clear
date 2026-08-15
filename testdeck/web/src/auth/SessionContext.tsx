import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useState,
  type ReactNode,
} from "react";
import { api, onUnauthorized } from "../api/client";
import type { Session } from "../api/types";

interface SessionState {
  session: Session | null; // null = still checking
  refresh: () => Promise<void>;
  logout: () => Promise<void>;
}

const Ctx = createContext<SessionState>({
  session: null,
  refresh: async () => {},
  logout: async () => {},
});

export function SessionProvider({ children }: { children: ReactNode }) {
  const [session, setSession] = useState<Session | null>(null);

  const refresh = useCallback(async () => {
    try {
      setSession(await api.get<Session>("/api/session"));
    } catch {
      setSession({ authenticated: false });
    }
  }, []);

  const logout = useCallback(async () => {
    try {
      await api.post("/api/logout");
    } finally {
      setSession({ authenticated: false });
    }
  }, []);

  useEffect(() => {
    void refresh();
    /* Any 401 from any call flips the app to the login screen. */
    return onUnauthorized(() => setSession({ authenticated: false }));
  }, [refresh]);

  return (
    <Ctx.Provider value={{ session, refresh, logout }}>{children}</Ctx.Provider>
  );
}

export function useSession() {
  return useContext(Ctx);
}
