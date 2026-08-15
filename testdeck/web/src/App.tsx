import { BrowserRouter, Route, Routes } from "react-router-dom";
import { SessionProvider, useSession } from "./auth/SessionContext";
import LoginPage from "./auth/LoginPage";
import AppShell from "./layout/AppShell";
import LaunchPage from "./pages/LaunchPage";
import LivePage from "./pages/LivePage";
import RosterPage from "./pages/RosterPage";
import HistoryPage from "./pages/HistoryPage";
import LogsPage from "./pages/LogsPage";
import { ErrorBoundary, Spinner, ToastProvider } from "./components/ui";

function Gate() {
  const { session } = useSession();
  if (session === null)
    return (
      <div className="flex min-h-dvh items-center justify-center">
        <Spinner label="connecting…" />
      </div>
    );
  if (!session.authenticated) return <LoginPage />;
  return (
    <Routes>
      <Route element={<AppShell />}>
        <Route index element={<LaunchPage />} />
        <Route path="live" element={<LivePage />} />
        <Route path="roster" element={<RosterPage />} />
        <Route path="history" element={<HistoryPage />} />
        <Route path="logs" element={<LogsPage />} />
        <Route path="*" element={<LaunchPage />} />
      </Route>
    </Routes>
  );
}

export default function App() {
  return (
    <ErrorBoundary>
      <BrowserRouter>
        <SessionProvider>
          <ToastProvider>
            <Gate />
          </ToastProvider>
        </SessionProvider>
      </BrowserRouter>
    </ErrorBoundary>
  );
}
