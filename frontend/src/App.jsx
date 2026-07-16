import React, { useState, useEffect, useRef } from 'react';
import { 
  Server, Activity, Terminal, FileText, Database, Shield, 
  Settings, Play, Square, RotateCcw, Plus, LogOut, Check,
  AlertTriangle, Cpu, HardDrive, RefreshCw, Trash2, UserCheck, 
  ArrowRight, ArrowLeft, Loader, Code, Download, Save, Users, Copy, Search, Bot, Send, Puzzle,
  Calendar, BarChart3, Clock, Zap, MessageSquare, Skull, TrendingUp, Power, X, Menu
} from 'lucide-react';
import { Line, Bar } from 'react-chartjs-2';
import { 
  Chart as ChartJS, CategoryScale, LinearScale, PointElement, 
  LineElement, BarElement, Title, Tooltip, Legend, Filler 
} from 'chart.js';

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, BarElement, Title, Tooltip, Legend, Filler);

const API_BASE = '/api';

const COMMON_ITEMS = [
  'minecraft:air',
  'minecraft:diamond_sword',
  'minecraft:netherite_sword',
  'minecraft:diamond_pickaxe',
  'minecraft:netherite_pickaxe',
  'minecraft:diamond_axe',
  'minecraft:elytra',
  'minecraft:golden_apple',
  'minecraft:enchanted_golden_apple',
  'minecraft:totem_of_undying',
  'minecraft:cooked_beef',
  'minecraft:ender_pearl',
  'minecraft:shield',
  'minecraft:potion',
  'minecraft:splash_potion',
  'minecraft:lingering_potion'
];

const cleanSubdomainInput = (val) => {
  return val.toLowerCase().replace(/[^a-z0-9-]/g, '').substring(0, 63);
};

const generateSubdomainFromName = (name) => {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9-]/g, '-')
    .replace(/-+/g, '-')
    .replace(/^-+|-+$/g, '')
    .substring(0, 63);
};

const parseInline = (text) => {
  if (!text) return '';
  const parts = text.split(/(\*\*.*?\*\*|`.*?`)/g);
  return parts.map((part, index) => {
    if (part.startsWith('**') && part.endsWith('**')) {
      return <strong key={index} className="font-extrabold text-white">{part.slice(2, -2)}</strong>;
    }
    if (part.startsWith('`') && part.endsWith('`')) {
      return <code key={index} className="bg-slate-900 border border-mcdeploy-border px-1.5 py-0.5 rounded font-mono text-xs text-mcdeploy-green mx-0.5">{part.slice(1, -1)}</code>;
    }
    return part;
  });
};

const renderMarkdown = (text) => {
  if (!text) return null;
  
  // Split by code blocks first
  const parts = text.split(/(```[\s\S]*?```)/g);
  
  return parts.map((part, index) => {
    if (part.startsWith('```')) {
      // It's a code block
      const match = part.match(/```(\w*)\n([\s\S]*?)```/);
      const language = match ? match[1] : '';
      const code = match ? match[2] : part.slice(3, -3);
      
      return (
        <div key={index} className="my-3 font-mono text-xs bg-slate-950 p-4 border border-mcdeploy-border rounded-lg overflow-x-auto text-left select-text">
          {language && <div className="text-[10px] text-mcdeploy-muted uppercase mb-1 font-bold">{language}</div>}
          <pre className="whitespace-pre">{code.trim()}</pre>
        </div>
      );
    }
    
    // It's regular text, parse line by line
    const lines = part.split('\n');
    return lines.map((line, lineIdx) => {
      // Headers
      if (line.startsWith('### ')) {
        return <h4 key={`${index}-${lineIdx}`} className="text-sm font-bold text-mcdeploy-green mt-3 mb-1.5 text-left">{parseInline(line.slice(4))}</h4>;
      }
      if (line.startsWith('## ')) {
        return <h3 key={`${index}-${lineIdx}`} className="text-base font-extrabold text-white mt-4 mb-2 text-left border-b border-mcdeploy-border/30 pb-1">{parseInline(line.slice(3))}</h3>;
      }
      if (line.startsWith('# ')) {
        return <h2 key={`${index}-${lineIdx}`} className="text-lg font-black text-white mt-5 mb-2.5 text-left">{parseInline(line.slice(2))}</h2>;
      }
      
      // Unordered Lists
      if (line.startsWith('- ') || line.startsWith('* ')) {
        return (
          <ul key={`${index}-${lineIdx}`} className="list-disc list-inside ml-4 my-1 text-left text-sm text-slate-300">
            <li>{parseInline(line.slice(2))}</li>
          </ul>
        );
      }
      
      // Ordered Lists
      const orderMatch = line.match(/^(\d+)\.\s(.*)/);
      if (orderMatch) {
        return (
          <ol key={`${index}-${lineIdx}`} className="list-decimal list-inside ml-4 my-1 text-left text-sm text-slate-300">
            <li>{parseInline(orderMatch[2])}</li>
          </ol>
        );
      }

      // Blockquote
      if (line.startsWith('> ')) {
        return (
          <blockquote key={`${index}-${lineIdx}`} className="border-l-4 border-mcdeploy-green bg-mcdeploy-bg/50 px-3 py-1.5 my-2 italic text-left text-sm text-slate-300 rounded-r">
            {parseInline(line.slice(2))}
          </blockquote>
        );
      }
      
      // Empty line
      if (line.trim() === '') {
        return <div key={`${index}-${lineIdx}`} className="h-2"></div>;
      }
      
      // Normal paragraph
      return <p key={`${index}-${lineIdx}`} className="text-sm my-1 text-left text-slate-200">{parseInline(line)}</p>;
    });
  });
};

// ============================================================
// Toast + Confirm modal â€” replaces every browser showToast()/confirm()
// throughout the dashboard with a polished in-app notification.
// ============================================================
function ToastStack({ toasts, onDismiss }) {
  return (
    <div className="fixed bottom-6 right-6 z-[9999] flex flex-col gap-3 max-w-sm pointer-events-none">
      {toasts.map(t => (
        <div
          key={t.id}
          className={`pointer-events-auto flex items-start gap-3 rounded-lg px-4 py-3 shadow-2xl border backdrop-blur-md animate-slide-in-right ${
            t.variant === 'error'   ? 'bg-red-950/95 border-red-800 text-red-50' :
            t.variant === 'success' ? 'bg-green-950/95 border-mcdeploy-green text-green-50' :
            t.variant === 'warning' ? 'bg-amber-950/95 border-amber-700 text-amber-50' :
                                       'bg-mcdeploy-card/95 border-mcdeploy-border text-white'
          }`}
        >
          <div className={`flex-shrink-0 mt-0.5 ${
            t.variant === 'error'   ? 'text-red-400' :
            t.variant === 'success' ? 'text-mcdeploy-green' :
            t.variant === 'warning' ? 'text-amber-400' :
                                       'text-mcdeploy-muted'
          }`}>
            {t.variant === 'error' ? (
              <AlertTriangle className="w-5 h-5" />
            ) : t.variant === 'success' ? (
              <Check className="w-5 h-5" />
            ) : t.variant === 'warning' ? (
              <AlertTriangle className="w-5 h-5" />
            ) : (
              <Bot className="w-5 h-5" />
            )}
          </div>
          <div className="flex-1 text-sm leading-snug whitespace-pre-wrap break-words">
            {t.title && <div className="font-bold mb-0.5">{t.title}</div>}
            {t.message}
          </div>
          <button
            onClick={() => onDismiss(t.id)}
            className="flex-shrink-0 opacity-60 hover:opacity-100 transition"
            aria-label="Dismiss notification"
          >
            <span className="text-lg leading-none">Ã—</span>
          </button>
        </div>
      ))}
    </div>
  );
}

function ConfirmDialog({ dialog }) {
  if (!dialog) return null;
  return (
    <div className="fixed inset-0 bg-black/70 z-[10000] flex items-center justify-center p-4 animate-fade-in">
      <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg max-w-md w-full p-6 shadow-2xl">
        <div className="flex items-start gap-3 mb-4">
          <div className={`w-10 h-10 rounded-full flex items-center justify-center flex-shrink-0 ${
            dialog.danger ? 'bg-red-900/50 text-red-400' : 'bg-mcdeploy-green/20 text-mcdeploy-green'
          }`}>
            <AlertTriangle className="w-5 h-5" />
          </div>
          <div className="flex-1">
            <h3 className="text-lg font-bold text-white mb-1">{dialog.title}</h3>
            <p className="text-sm text-mcdeploy-muted whitespace-pre-wrap">{dialog.message}</p>
          </div>
        </div>
        <div className="flex items-center justify-end gap-2 mt-6">
          <button
            onClick={dialog.onCancel}
            className="bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-white text-sm font-semibold py-2 px-4 rounded transition"
          >
            {dialog.cancelLabel || 'Cancel'}
          </button>
          <button
            onClick={dialog.onConfirm}
            className={`text-black text-sm font-bold py-2 px-4 rounded transition ${
              dialog.danger
                ? 'bg-red-500 hover:bg-red-400 text-white'
                : 'bg-mcdeploy-green hover:bg-mcdeploy-green/80'
            }`}
          >
            {dialog.confirmLabel || 'Confirm'}
          </button>
        </div>
      </div>
    </div>
  );
}

export default function App() {
  // Toasts + confirm modal state
  const [toasts, setToasts] = useState([]);
  const [confirmDialog, setConfirmDialog] = useState(null);

  // showToast(message, options?) â€” options: { variant, title, duration }
  // Auto-detects error/success from the message content when no variant is given.
  const showToast = React.useCallback((message, options = {}) => {
    if (message == null) return;
    const msg = String(message);
    let variant = options.variant;
    if (!variant) {
      const lower = msg.toLowerCase();
      if (/error|failed?|invalid|denied|unable|couldn't|cannot|fail|missing|already exists/.test(lower)) variant = 'error';
      else if (/success|saved|copied|created|updated|granted|restored|installed|deleted|repaired|transferred|uploaded/.test(lower)) variant = 'success';
      else if (/please|select|specify|enter/.test(lower)) variant = 'warning';
      else variant = 'info';
    }
    const duration = options.duration ?? (variant === 'error' ? 8000 : 4200);
    const id = Date.now() + Math.random();
    setToasts(prev => [...prev, { id, message: msg, title: options.title, variant, duration }]);
    setTimeout(() => setToasts(prev => prev.filter(t => t.id !== id)), duration);
  }, []);

  // Callback-based confirm dialog. Signature:
  //   showConfirm({ title, message, danger, onConfirm, onCancel, confirmLabel, cancelLabel })
  const showConfirm = React.useCallback((opts) => {
    setConfirmDialog({
      title: opts.title || 'Are you sure?',
      message: opts.message || '',
      danger: opts.danger || false,
      confirmLabel: opts.confirmLabel,
      cancelLabel: opts.cancelLabel,
      onConfirm: () => { setConfirmDialog(null); if (opts.onConfirm) opts.onConfirm(); },
      onCancel:  () => { setConfirmDialog(null); if (opts.onCancel)  opts.onCancel();  }
    });
  }, []);

  const [token, setToken] = useState(localStorage.getItem('mcdeploy_token') || 'dummy_token');
  const [username, setUsername] = useState(localStorage.getItem('mcdeploy_user') || 'admin');
  const [role, setRole] = useState(localStorage.getItem('mcdeploy_role') || 'admin');
  
  const [loginUser, setLoginUser] = useState('');
  const [loginPass, setLoginPass] = useState('');
  const [loginError, setLoginError] = useState('');

  const [activeTab, setActiveTab] = useState('overview'); // overview, installer, audit, server-[uuid]
  const [serverTab, setServerTab] = useState('console'); // console, files, config, backups, metrics, ai
  const [mobileNavOpen, setMobileNavOpen] = useState(false);
  
  const [servers, setServers] = useState([]);
  const [selectedServer, setSelectedServer] = useState(null);
  const [systemMetrics, setSystemMetrics] = useState(null);
  const [auditLogs, setAuditLogs] = useState([]);
  
  // Refresh toggles
  const [refreshTrigger, setRefreshTrigger] = useState(0);

  // Installer state
  const [installStep, setInstallStep] = useState(1);
  const [installName, setInstallName] = useState('');
  const [installSoftware, setInstallSoftware] = useState('paper');
  const [installVersion, setInstallVersion] = useState('');
  const [installVersionsList, setInstallVersionsList] = useState([]);
  const [installPort, setInstallPort] = useState(25565);
  const [installRamMin, setInstallRamMin] = useState(1024);
  const [installRamMax, setInstallRamMax] = useState(2048);
  const [installDifficulty, setInstallDifficulty] = useState('easy');
  const [installGamemode, setInstallGamemode] = useState('survival');
  const [installOnlineMode, setInstallOnlineMode] = useState(true);
  const [modpackSource, setModpackSource] = useState('modrinth');
  const [curseforgeApiKey, setCurseforgeApiKey] = useState('');
  const [modpackQuery, setModpackQuery] = useState('');
  const [modpackSearchResults, setModpackSearchResults] = useState([]);
  const [searchingModpacks, setSearchingModpacks] = useState(false);
  const [selectedModpack, setSelectedModpack] = useState(null);
  const [selectedModpackVersion, setSelectedModpackVersion] = useState(null);
  const [showSearchModal, setShowSearchModal] = useState(false);
  const [modalModpacks, setModalModpacks] = useState([]);
  const [modalOffset, setModalOffset] = useState(0);
  const [modalHasMore, setModalHasMore] = useState(true);
  const [modalLoading, setModalLoading] = useState(false);

  // Addon Installer states
  const [addonQuery, setAddonQuery] = useState('');
  const [addonSource, setAddonSource] = useState('modrinth');
  const [addonSearchResults, setAddonSearchResults] = useState([]);
  const [searchingAddons, setSearchingAddons] = useState(false);
  const [selectedAddon, setSelectedAddon] = useState(null);
  const [addonVersionsList, setAddonVersionsList] = useState([]);
  const [fetchingAddonVersions, setFetchingAddonVersions] = useState(false);
  const [selectedAddonVersion, setSelectedAddonVersion] = useState(null);
  const [installedAddons, setInstalledAddons] = useState([]);
  const [addonStatusMsg, setAddonStatusMsg] = useState('');
  const [installingAddon, setInstallingAddon] = useState(false);
  const [uninstallingAddon, setUninstallingAddon] = useState(null);
  const [installLogs, setInstallLogs] = useState('Waiting to start...');
  const [installing, setInstalling] = useState(false);
  const [installSubdomain, setInstallSubdomain] = useState('');
  const [isSubdomainEdited, setIsSubdomainEdited] = useState(false);
  const [subdomainStatus, setSubdomainStatus] = useState('idle');
  const [subdomainMessage, setSubdomainMessage] = useState('');
  const [showDeleteModal, setShowDeleteModal] = useState(null);
  const [deleteConfirmName, setDeleteConfirmName] = useState('');

  // File Manager State
  const [filesList, setFilesList] = useState([]);
  const [editingFile, setEditingFile] = useState(null);
  const [editingContent, setEditingContent] = useState('');
  
  // Config form state
  const [configProperties, setConfigProperties] = useState({});
  
  // Backups state
  const [backupsList, setBackupsList] = useState([]);
  
  // AI Editor state
  const [aiMessages, setAiMessages] = useState([]);
  const [aiInput, setAiInput] = useState('');
  const [aiAgentMode, setAiAgentMode] = useState(false);
  const [aiLoading, setAiLoading] = useState(false);
  const [aiPendingActions, setAiPendingActions] = useState([]); // dangerous tools awaiting approval
  const [aiUsage, setAiUsage] = useState({ tokens_total: 0, request_count: 0, undo_stack_size: 0, last_model: '' });
  const [aiShowSteps, setAiShowSteps] = useState(true); // toggle the tool-trace panel per message
  const [aiSlashOpen, setAiSlashOpen] = useState(false); // slash command palette
  const [aiConfirmModal, setAiConfirmModal] = useState(null); // { tool, arguments, reason, diff? }
  const aiEndRef = useRef(null);

  // Available slash commands rendered in the palette
  const AI_SLASH_COMMANDS = [
    { cmd: '/logs',       hint: 'Show recent server logs' },
    { cmd: '/errors',     hint: 'Search logs for errors' },
    { cmd: '/plugins',    hint: 'List installed plugins/mods' },
    { cmd: '/players',    hint: 'List players + status' },
    { cmd: '/metrics',    hint: 'Current CPU / RAM / TPS' },
    { cmd: '/info',       hint: 'Server metadata' },
    { cmd: '/files',      hint: 'List server directory' },
    { cmd: '/restart',    hint: 'Restart the server (asks for confirmation)' },
    { cmd: '/undo',       hint: 'Undo the last AI file change' },
    { cmd: '/clear',      hint: 'Clear conversation history' },
  ];

  // Live monitor historical statistics
  const [historyCpu, setHistoryCpu] = useState(new Array(30).fill(0));
  const [historyRam, setHistoryRam] = useState(new Array(30).fill(0));
  const [historyTps, setHistoryTps] = useState(new Array(30).fill(20));
  const [serverMetrics, setServerMetrics] = useState(null);

  // Performance tuning state
  const [perfPriority, setPerfPriority] = useState('normal');
  const [perfSmartOpt, setPerfSmartOpt] = useState(true);
  const [perfLoading, setPerfLoading] = useState(false);

  // Operations features state
  const [importDirectory, setImportDirectory] = useState('');
  const [importDisplayName, setImportDisplayName] = useState('');
  const [importingServer, setImportingServer] = useState(false);
  const [healthData, setHealthData] = useState(null);
  const [healthLoading, setHealthLoading] = useState(false);
  const [automationRules, setAutomationRules] = useState([]);
  const [automationLoading, setAutomationLoading] = useState(false);
  const [automationForm, setAutomationForm] = useState({
    name: 'Recover offline server', trigger_type: 'server_offline', threshold: 90,
    condition_value: '', action_type: 'start', action_payload: '', cooldown_seconds: 300
  });
  const [maintenance, setMaintenance] = useState({
    enabled: false, message: 'Server maintenance is in progress. Please check back soon.',
    prevent_joins: true, backup_on_enable: true, enabled_at: ''
  });
  const [maintenanceLoading, setMaintenanceLoading] = useState(false);

  // Webpanel access management state
  const [webpanelMembers, setWebpanelMembers] = useState([]);
  const [webpanelCatalog, setWebpanelCatalog] = useState({ groups: [], role_presets: {} });
  const [webpanelLoading, setWebpanelLoading] = useState(false);
  const [webpanelSearch, setWebpanelSearch] = useState('');
  const [webpanelInvite, setWebpanelInvite] = useState({
    email: '', display_name: '', server_uuid: '', role: 'viewer'
  });
  const [webpanelPermissionEditor, setWebpanelPermissionEditor] = useState(null);

  // Scheduled Tasks state
  const [scheduleTasks, setScheduleTasks] = useState([]);
  const [scheduleLoading, setScheduleLoading] = useState(false);
  const [scheduleRunsFor, setScheduleRunsFor] = useState(null); // task id whose history panel is open
  const [scheduleRuns, setScheduleRuns] = useState([]);
  const [scheduleForm, setScheduleForm] = useState(null); // null = closed. object = editing
  const emptyScheduleForm = {
    id: null,
    name: '',
    action_type: 'restart',
    payload: '',
    schedule_kind: 'daily',
    schedule_value: '04:00',
    enabled: true,
  };

  // Analytics state
  const [analyticsDays, setAnalyticsDays] = useState(7);
  const [analyticsSummary, setAnalyticsSummary] = useState(null);
  const [analyticsHourly, setAnalyticsHourly] = useState([]);
  const [analyticsDaily, setAnalyticsDaily] = useState([]);
  const [analyticsLeaderboard, setAnalyticsLeaderboard] = useState([]);
  const [analyticsEvents, setAnalyticsEvents] = useState([]);
  const [analyticsEventType, setAnalyticsEventType] = useState('chat');
  const [analyticsLoading, setAnalyticsLoading] = useState(false);

  // WebSockets refs
  const consoleWsRef = useRef(null);
  const monitorWsRef = useRef(null);
  
  // Console state
  const [consoleLogs, setConsoleLogs] = useState([]);
  const [commandInput, setCommandInput] = useState('');
  const consoleEndRef = useRef(null);

  // Player Manager State
  const [playersList, setPlayersList] = useState([]);
  const [selectedPlayer, setSelectedPlayer] = useState(null);
  const [coordinateLogs, setCoordinateLogs] = useState([]);
  const [playerInventory, setPlayerInventory] = useState([]);
  const [playerEnderChest, setPlayerEnderChest] = useState([]);
  const [playerBackups, setPlayerBackups] = useState([]);
  const [playerAdvancements, setPlayerAdvancements] = useState([]);
  const [playerDetailTab, setPlayerDetailTab] = useState('inventory'); // inventory, ender_chest, potion, advancements, backups, coordinates
  const [selectedItemSlot, setSelectedItemSlot] = useState(null); // { type, slot, item }
  const [editItemName, setEditItemName] = useState('');
  const [editItemCount, setEditItemCount] = useState(1);
  const [editItemUnbreakable, setEditItemUnbreakable] = useState(false);
  const [editItemAura, setEditItemAura] = useState('');
  const [editItemPotion, setEditItemPotion] = useState('');
  const [editItemEnchants, setEditItemEnchants] = useState({});
  const [newEnchantName, setNewEnchantName] = useState('minecraft:sharpness');
  const [newEnchantLevel, setNewEnchantLevel] = useState(1);
  const [giveTargetPlayer, setGiveTargetPlayer] = useState('');
  const [potionEffect, setPotionEffect] = useState('minecraft:speed');
  const [potionDuration, setPotionDuration] = useState(30);
  const [potionAmplifier, setPotionAmplifier] = useState(1);
  const [playerBackupName, setPlayerBackupName] = useState('');
  const [actionReason, setActionReason] = useState('');
  const [actionDuration, setActionDuration] = useState('1h');
  const [showActionModal, setShowActionModal] = useState(null); // 'kick', 'ban', 'tempban', 'timeout'

  // Auth header helper
  const authHeaders = {
    'Content-Type': 'application/json',
    'Authorization': `Bearer ${token}`
  };

  // Check globally authoritative subdomain availability through the local backend.
  useEffect(() => {
    if (!installSubdomain.trim()) {
      setSubdomainStatus('idle');
      setSubdomainMessage('');
      return;
    }

    const reg = /^[a-z0-9]([a-z0-9-]{1,61}[a-z0-9])?$/;
    if (!reg.test(installSubdomain.toLowerCase())) {
      setSubdomainStatus('invalid');
      setSubdomainMessage('Invalid format. Use 3-63 lowercase letters, numbers, and hyphens.');
      return;
    }

    const reserved = [
      "www", "mail", "ftp", "admin", "api", "app", "panel",
      "dashboard", "status", "blog", "docs", "help", "support",
      "ns1", "ns2", "mx", "smtp", "imap", "pop", "mcdeploy", "offline"
    ];
    if (reserved.includes(installSubdomain.toLowerCase())) {
      setSubdomainStatus('invalid');
      setSubdomainMessage('Reserved subdomain.');
      return;
    }

    setSubdomainStatus('checking');
    setSubdomainMessage('Checking global availability...');
    const controller = new AbortController();
    const checkedName = installSubdomain.toLowerCase();

    const timer = setTimeout(() => {
      fetch(`${API_BASE}/servers/check-subdomain?name=${encodeURIComponent(checkedName)}`, {
        headers: authHeaders,
        signal: controller.signal
      })
      .then(async res => {
        const data = await res.json().catch(() => ({}));
        if (!res.ok) throw new Error(data.message || `Availability service returned ${res.status}`);
        return data;
      })
      .then(data => {
        if (data.available) {
          setSubdomainStatus('available');
          setSubdomainMessage(data.message || 'Subdomain is globally available!');
        } else {
          setSubdomainStatus('taken');
          setSubdomainMessage(data.message || 'Subdomain is already reserved.');
        }
      })
      .catch(err => {
        if (err.name === 'AbortError') return;
        setSubdomainStatus('taken');
        setSubdomainMessage(`Global availability could not be confirmed: ${err.message}`);
      });
    }, 500);

    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [installSubdomain, token]);

  const handleNameChange = (val) => {
    setInstallName(val);
    if (!isSubdomainEdited) {
      setInstallSubdomain(generateSubdomainFromName(val));
    }
  };

  const handleSubdomainChange = (val) => {
    setIsSubdomainEdited(true);
    setInstallSubdomain(cleanSubdomainInput(val));
  };

  // Fetch servers & system metrics
  useEffect(() => {
    if (!token) return;

    fetch(`${API_BASE}/servers`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setServers(data);
        if (activeTab.startsWith('server-')) {
          const uuid = activeTab.replace('server-', '');
          const match = data.find(s => s.uuid === uuid);
          if (match) setSelectedServer(match);
        }
      })
      .catch(err => console.error(err));

    fetch(`${API_BASE}/system/metrics`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setSystemMetrics(data);
        if (data.host_metrics) {
          setHistoryCpu(prev => [...prev.slice(1), data.host_metrics.cpu_usage]);
          setHistoryRam(prev => [...prev.slice(1), (data.host_metrics.ram_used / data.host_metrics.ram_total) * 100]);
        }
      })
      .catch(err => console.error(err));

  }, [token, activeTab, refreshTrigger]);

  // Fetch audit logs
  useEffect(() => {
    if (!token || activeTab !== 'audit') return;
    fetch(`${API_BASE}/system/audit-logs`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => setAuditLogs(data))
      .catch(err => console.error(err));
  }, [token, activeTab, refreshTrigger]);

  // Fetch installer versions
  useEffect(() => {
    if (!token || activeTab !== 'installer' || installSoftware === 'modpack') return;
    fetch(`${API_BASE}/servers/software/versions?software=${installSoftware}`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setInstallVersionsList(data);
        if (data.length > 0) setInstallVersion(data[0]);
      })
      .catch(err => console.error(err));
  }, [token, installSoftware, activeTab]);

  // WebSocket subscriptions for Monitor and Console
  useEffect(() => {
    if (!token) return;

    // 1. Setup Monitor WebSocket
    if (monitorWsRef.current) monitorWsRef.current.close();
    
    const loc = window.location;
    const wsScheme = loc.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsHost = loc.host || 'localhost:8082';
    
    const monitorWs = new WebSocket(`${wsScheme}//${wsHost}/api/servers/monitor`);
    monitorWsRef.current = monitorWs;

    monitorWs.onopen = () => {
      const subUuid = selectedServer ? selectedServer.uuid : "";
      monitorWs.send(JSON.stringify({ action: "subscribe", uuid: subUuid }));
    };

    monitorWs.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === "metrics") {
          setServerMetrics(msg.data);
          if (msg.data.tps) {
            setHistoryTps(prev => [...prev.slice(1), msg.data.tps]);
          }
          if (!selectedServer) {
            setHistoryCpu(prev => [...prev.slice(1), msg.data.cpu_usage]);
            setHistoryRam(prev => [...prev.slice(1), (msg.data.ram_used / msg.data.ram_total) * 100]);
          }
        }
      } catch (err) {
        console.error(err);
      }
    };

    // 2. Setup Console WebSocket if a server is selected
    if (selectedServer && activeTab.startsWith('server-')) {
      if (consoleWsRef.current) consoleWsRef.current.close();
      
      const consoleWs = new WebSocket(`${wsScheme}//${wsHost}/api/servers/console`);
      consoleWsRef.current = consoleWs;
      setConsoleLogs([]);

      consoleWs.onopen = () => {
        consoleWs.send(JSON.stringify({ action: "subscribe", uuid: selectedServer.uuid, token }));
      };

      consoleWs.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          if (msg.type === "logs") {
            setConsoleLogs(prev => [...prev, ...msg.data].slice(-300));
          }
        } catch (err) {
          console.error(err);
        }
      };
    }

    return () => {
      if (consoleWsRef.current) consoleWsRef.current.close();
      if (monitorWsRef.current) monitorWsRef.current.close();
    };

  }, [token, selectedServer, activeTab]);

  // Scroll to console bottom
  useEffect(() => {
    if (consoleEndRef.current) {
      consoleEndRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [consoleLogs]);
  
  // Scroll to AI chat bottom
  useEffect(() => {
    if (aiEndRef.current) {
      aiEndRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [aiMessages, aiPendingActions, aiLoading]);

  // Load conversation + usage when the AI tab opens for a server.
  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'ai' && selectedServer) {
      loadAiConversation();
      refreshAiUsage();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeTab, serverTab, selectedServer && selectedServer.uuid]);

  // Handle Login
  const handleLogin = (e) => {
    e.preventDefault();
    setLoginError('');
    fetch(`${API_BASE}/auth/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: loginUser, password: loginPass })
    })
    .then(res => {
      if (!res.ok) throw new Error('Invalid credentials');
      return res.json();
    })
    .then(data => {
      if (data.status === 'success') {
        localStorage.setItem('mcdeploy_token', data.token);
        localStorage.setItem('mcdeploy_user', data.username);
        localStorage.setItem('mcdeploy_role', data.role);
        setToken(data.token);
        setUsername(data.username);
        setRole(data.role);
        setActiveTab('overview');
      }
    })
    .catch(err => {
      setLoginError(err.message);
    });
  };

  const handleLogout = () => {
    localStorage.removeItem('mcdeploy_token');
    localStorage.removeItem('mcdeploy_user');
    localStorage.removeItem('mcdeploy_role');
    setToken('');
    setUsername('');
    setRole('');
  };

  // Quick Controls
  const triggerControl = (uuid, action) => {
    fetch(`${API_BASE}/servers/${uuid}/control`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({ action })
    })
    .then(() => setRefreshTrigger(p => p + 1))
    .catch(err => console.error(err));
  };

  // Modpack Handlers
  const handleSearchModpacks = () => {
    if (!modpackQuery.trim()) return;
    setSearchingModpacks(true);
    setModpackSearchResults([]);
    setSelectedModpack(null);
    setSelectedModpackVersion(null);
    
    fetch(`${API_BASE}/modpacks/search?source=${modpackSource}&query=${encodeURIComponent(modpackQuery)}&apiKey=${encodeURIComponent(curseforgeApiKey)}`, {
      headers: authHeaders
    })
    .then(res => res.json())
    .then(data => {
      setModpackSearchResults(data);
      setSearchingModpacks(false);
    })
    .catch(err => {
      console.error(err);
      setSearchingModpacks(false);
    });
  };

  const handleSelectModpack = (pack) => {
    setSelectedModpack(pack);
    setSelectedModpackVersion(null);
    setInstallVersionsList([]);
    setInstallVersion('');
    
    fetch(`${API_BASE}/modpacks/${pack.id}/versions?source=${modpackSource}&apiKey=${encodeURIComponent(curseforgeApiKey)}`, {
      headers: authHeaders
    })
    .then(res => res.json())
    .then(data => {
      setInstallVersionsList(data);
      const firstWithServer = data.find(v => v.hasServerPack);
      if (firstWithServer) {
        setSelectedModpackVersion(firstWithServer);
        setInstallVersion(firstWithServer.id);
      } else if (data.length > 0) {
        setSelectedModpackVersion(data[0]);
        setInstallVersion(data[0].id);
      }
    })
    .catch(err => console.error(err));
  };

  const openSearchModal = () => {
    setModalModpacks(modpackSearchResults);
    setModalOffset(modpackSearchResults.length);
    setModalHasMore(modpackSearchResults.length >= 20);
    setShowSearchModal(true);
  };

  const handleLoadMoreModpacks = () => {
    if (modalLoading || !modalHasMore || !modpackQuery.trim()) return;
    setModalLoading(true);
    const nextOffset = modalOffset;
    
    fetch(`${API_BASE}/modpacks/search?source=${modpackSource}&query=${encodeURIComponent(modpackQuery)}&apiKey=${encodeURIComponent(curseforgeApiKey)}&offset=${nextOffset}&limit=20`, {
      headers: authHeaders
    })
    .then(res => res.json())
    .then(data => {
      if (data.length === 0) {
        setModalHasMore(false);
      } else {
        setModalModpacks(prev => [...prev, ...data]);
        setModalOffset(prev => prev + data.length);
        if (data.length < 20) {
          setModalHasMore(false);
        }
      }
      setModalLoading(false);
    })
    .catch(err => {
      console.error(err);
      setModalLoading(false);
    });
  };

  // Create Server
  const handleInstallServer = () => {
    setInstalling(true);
    setInstallLogs('Initializing wizard configuration...');
    
    fetch(`${API_BASE}/servers`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        name: installName,
        subdomain: installSubdomain,
        software_type: installSoftware,
        version: installVersion,
        port: installPort,
        ram_min: installRamMin,
        ram_max: installRamMax,
        difficulty: installDifficulty,
        gamemode: installGamemode,
        online_mode: installOnlineMode,
        modpack_source: modpackSource,
        modpack_id: selectedModpack ? selectedModpack.id : '',
        modpack_version_id: selectedModpackVersion ? selectedModpackVersion.id : '',
        server_pack_url: selectedModpackVersion ? selectedModpackVersion.serverPackUrl : '',
        server_pack_file_id: selectedModpackVersion ? selectedModpackVersion.serverPackFileId : '',
        game_version: selectedModpackVersion ? (selectedModpackVersion.gameVersions?.[0] || '1.20.1') : '',
        loader_type: selectedModpackVersion ? (
          selectedModpackVersion.loaders?.includes('neoforge') ? 'neoforge' :
          selectedModpackVersion.loaders?.includes('forge') ? 'forge' :
          selectedModpackVersion.loaders?.includes('fabric') ? 'fabric' :
          (selectedModpackVersion.loaders?.[0] || 'fabric')
        ) : '',
        apiKey: curseforgeApiKey
      })
    })
    .then(async res => ({ status: res.status, ok: res.ok, data: await res.json().catch(() => ({})) }))
    .then(({ status, ok, data }) => {
      if (ok && data.status === 'success') {
        setInstallLogs(prev => prev + '\nâœ“ Server configuration saved.\nDownloading file and starting setup script in background...');
        setTimeout(() => {
          setInstalling(false);
          setActiveTab('overview');
          setRefreshTrigger(p => p + 1);
        }, 3000);
      } else {
        if (status === 409) {
          setSubdomainStatus('taken');
          setSubdomainMessage(data.message || 'This subdomain was just reserved by another server.');
        }
        setInstallLogs(prev => prev + `\nâœ– Error: ${data.message || `Request failed (${status})`}`);
        setInstalling(false);
      }
    })
    .catch(err => {
      setInstallLogs(prev => prev + `\nâœ– Network Error: ${err.message}`);
      setInstalling(false);
    });
  };

  // Delete Server
  const handleDeleteServer = (uuid) => {
    fetch(`${API_BASE}/servers/${uuid}`, {
      method: 'DELETE',
      headers: authHeaders
    })
    .then(() => {
      setActiveTab('overview');
      setSelectedServer(null);
      setRefreshTrigger(p => p + 1);
    });
  };

  // Live Console Command Send
  const handleSendCommand = (e) => {
    e.preventDefault();
    if (!commandInput.trim() || !consoleWsRef.current) return;
    
    consoleWsRef.current.send(JSON.stringify({
      action: 'command',
      command: commandInput
    }));
    
    setCommandInput('');
  };
  
  // Slash-command â†’ natural-language mapping. Returns null if not a slash cmd.
  const resolveSlashCommand = (raw) => {
    if (!raw.startsWith('/')) return null;
    const parts = raw.trim().split(/\s+/);
    const cmd = parts[0].toLowerCase();
    const rest = parts.slice(1).join(' ');
    switch (cmd) {
      case '/logs':    return 'Show me the last 30 log lines.';
      case '/errors':  return 'Search server logs for errors from the last run.';
      case '/plugins': return 'What plugins or mods are installed on this server?';
      case '/players': return 'List all players and their online status.';
      case '/metrics': return 'Give me the current CPU, RAM, and TPS for this server.';
      case '/info':    return 'Show me the server metadata (name, version, port, etc).';
      case '/files':   return 'List files in the server root directory.';
      case '/restart': return 'Please restart the server.';
      case '/diff':    return rest ? `Show me a diff for ${rest} using the current file.` : '/diff needs a filename.';
      case '/undo':    return '__UNDO__';
      case '/clear':   return '__CLEAR__';
      default: return null;
    }
  };

  // Refresh AI usage stats (tokens / undo stack size)
  const refreshAiUsage = () => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/ai/usage`, { headers: authHeaders })
      .then(r => r.json())
      .then(d => { if (d.status === 'success') setAiUsage(d); })
      .catch(() => {});
  };

  // Load persisted conversation from server
  const loadAiConversation = () => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/ai/conversation`, { headers: authHeaders })
      .then(r => r.json())
      .then(d => {
        if (d.status === 'success' && Array.isArray(d.conversation)) {
          const visible = d.conversation
            .filter(m => m.role === 'user' || (m.role === 'assistant' && m.content))
            .map(m => ({
              role: m.role,
              content: m.content,
              timestamp: m.created_at ? new Date(m.created_at + 'Z').toLocaleTimeString() : new Date().toLocaleTimeString()
            }));
          setAiMessages(visible);
        }
      })
      .catch(() => {});
  };

  // AI Editor Chat Handler
  const handleAiChat = (e) => {
    if (e && e.preventDefault) e.preventDefault();
    if (!aiInput.trim() || !selectedServer || aiLoading) return;

    const raw = aiInput.trim();
    setAiInput('');
    setAiSlashOpen(false);

    // Handle client-side slash commands
    const resolved = resolveSlashCommand(raw);
    if (resolved === '__CLEAR__') { handleClearAiChat(); return; }
    if (resolved === '__UNDO__')  { handleAiUndo(); return; }
    const userMessage = resolved || raw;

    setAiLoading(true);
    setAiMessages(prev => [...prev, { role: 'user', content: raw, timestamp: new Date().toLocaleTimeString() }]);

    fetch(`${API_BASE}/servers/${selectedServer.uuid}/ai/chat`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({ message: userMessage, agent_mode: aiAgentMode })
    })
    .then(res => res.json())
    .then(data => {
      setAiLoading(false);
      if (data.status === 'success') {
        setAiMessages(prev => [...prev, {
          role: 'assistant',
          content: data.message,
          steps: data.steps || [],
          suggestions: data.suggestions || [],
          tokens: data.tokens || null,
          timestamp: new Date().toLocaleTimeString()
        }]);
        if (Array.isArray(data.pending_actions) && data.pending_actions.length > 0) {
          setAiPendingActions(prev => [...prev, ...data.pending_actions]);
          setAiConfirmModal(data.pending_actions[0]);
        }
        refreshAiUsage();
      } else {
        setAiMessages(prev => [...prev, {
          role: 'error',
          content: (data.message || data.error) || 'AI request failed',
          timestamp: new Date().toLocaleTimeString()
        }]);
      }
    })
    .catch(err => {
      setAiLoading(false);
      const raw = err && err.message ? err.message : String(err);
      // "Failed to fetch" from the browser typically means the backend socket
      // closed mid-flight (mcdeploy.exe crashed / was killed / never started).
      const friendly = /failed to fetch|networkerror|load failed/i.test(raw)
        ? 'Cannot reach the MCDeploy backend. Is mcdeploy.exe still running? Try relaunching it, then hard-refresh this page (Ctrl+Shift+R).'
        : ('Network error: ' + raw);
      setAiMessages(prev => [...prev, { role: 'error', content: friendly, timestamp: new Date().toLocaleTimeString() }]);
    });
  };

  // Approve a pending dangerous action
  const handleApproveAction = (action) => {
    if (!selectedServer || !action) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/ai/approve`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({ tool: action.tool, arguments: action.arguments })
    })
    .then(r => r.json())
    .then(data => {
      const result = data.step?.result || data.message || 'Executed.';
      setAiMessages(prev => [...prev, {
        role: 'assistant',
        content: `**Executed \`${action.tool}\`.**\n\n\`\`\`\n${result}\n\`\`\``,
        timestamp: new Date().toLocaleTimeString()
      }]);
      setAiPendingActions(prev => prev.filter(a => a !== action));
      setAiConfirmModal(null);
      refreshAiUsage();
    })
    .catch(err => {
      setAiMessages(prev => [...prev, { role: 'error', content: 'Approval failed: ' + err.message, timestamp: new Date().toLocaleTimeString() }]);
      setAiConfirmModal(null);
    });
  };

  const handleRejectAction = (action) => {
    setAiPendingActions(prev => prev.filter(a => a !== action));
    setAiConfirmModal(null);
    setAiMessages(prev => [...prev, {
      role: 'assistant',
      content: `Skipped \`${action.tool}\`. Let me know if you want to try something else.`,
      timestamp: new Date().toLocaleTimeString()
    }]);
  };

  // Undo the most recent AI-caused file change
  const handleAiUndo = () => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/ai/undo`, { method: 'POST', headers: authHeaders })
      .then(r => r.json())
      .then(d => {
        setAiMessages(prev => [...prev, {
          role: d.status === 'success' ? 'assistant' : 'error',
          content: d.message || 'Undo failed.',
          timestamp: new Date().toLocaleTimeString()
        }]);
        refreshAiUsage();
      })
      .catch(err => {
        setAiMessages(prev => [...prev, { role: 'error', content: 'Undo failed: ' + err.message, timestamp: new Date().toLocaleTimeString() }]);
      });
  };

  const handleClearAiChat = () => {
    if (!selectedServer) return;
    setAiMessages([]);
    setAiPendingActions([]);
    setAiConfirmModal(null);
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/ai/conversation`, { method: 'DELETE', headers: authHeaders })
      .then(refreshAiUsage)
      .catch(err => console.error(err));
  };

  // File Manager: List Files
  const fetchFiles = () => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/files`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => setFilesList(data))
      .catch(err => console.error(err));
  };

  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'files') {
      fetchFiles();
      setEditingFile(null);
    }
  }, [activeTab, serverTab]);

  const handleEditFile = (name) => {
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/files/view?file=${name}`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setEditingFile(name);
        setEditingContent(data.content);
      })
      .catch(err => console.error(err));
  };

  const handleSaveFile = () => {
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/files/save`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({ file: editingFile, content: editingContent })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        showToast('File saved successfully!');
        setEditingFile(null);
        fetchFiles();
      }
    })
    .catch(err => console.error(err));
  };

  // Config: Read Properties
  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'config' && selectedServer) {
      fetch(`${API_BASE}/servers/${selectedServer.uuid}/config`, { headers: authHeaders })
        .then(res => res.json())
        .then(data => setConfigProperties(data))
        .catch(err => console.error(err));
    }
  }, [activeTab, serverTab]);

  const handleSaveConfig = (e) => {
    e.preventDefault();
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/config`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify(configProperties)
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        showToast('Server properties updated!');
      }
    })
    .catch(err => console.error(err));
  };

  // Backups: Read List
  const fetchBackups = () => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/backups`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => setBackupsList(data))
      .catch(err => console.error(err));
  };

  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'backups') {
      fetchBackups();
    }
  }, [activeTab, serverTab]);

  const handleCreateBackup = () => {
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/backups`, {
      method: 'POST',
      headers: authHeaders
    })
    .then(res => res.json())
    .then(() => fetchBackups())
    .catch(err => console.error(err));
  };

  // Performance Optimizer Handlers
  const fetchPerformanceSettings = () => {
    if (!selectedServer) return;
    setPerfLoading(true);
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/performance`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setPerfPriority(data.cpu_priority || 'normal');
        setPerfSmartOpt(data.smart_optimization !== false);
        setPerfLoading(false);
      })
      .catch(err => {
        console.error('Error fetching performance settings:', err);
        setPerfLoading(false);
      });
  };

  const handleUpdatePerformance = (smartOpt, priority) => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/performance`, {
      method: 'POST',
      headers: { ...authHeaders, 'Content-Type': 'application/json' },
      body: JSON.stringify({
        smart_optimization: smartOpt,
        cpu_priority: priority
      })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        setPerfSmartOpt(smartOpt);
        setPerfPriority(priority);
      }
    })
    .catch(err => console.error('Error updating performance settings:', err));
  };

  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'metrics' && selectedServer) {
      fetchPerformanceSettings();
    }
  }, [activeTab, serverTab, selectedServer]);

  // Addon Installer Handlers
  const fetchInstalledAddons = () => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/addons/installed`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => setInstalledAddons(data))
      .catch(err => console.error('Error fetching installed addons:', err));
  };

  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'addons' && selectedServer) {
      fetchInstalledAddons();
    }
  }, [activeTab, serverTab, selectedServer]);

  const handleSearchAddons = () => {
    if (!addonQuery.trim() || !selectedServer) return;
    setSearchingAddons(true);
    setAddonSearchResults([]);
    setSelectedAddon(null);
    setSelectedAddonVersion(null);
    setAddonStatusMsg('');

    fetch(`${API_BASE}/servers/${selectedServer.uuid}/addons/search?source=${addonSource}&query=${encodeURIComponent(addonQuery)}&apiKey=${encodeURIComponent(curseforgeApiKey)}`, {
      headers: authHeaders
    })
      .then(res => {
        if (!res.ok) {
          return res.json().then(errData => {
            throw new Error(errData.message || 'Search failed');
          });
        }
        return res.json();
      })
      .then(data => {
        setAddonSearchResults(data);
        setSearchingAddons(false);
      })
      .catch(err => {
        console.error(err);
        setAddonStatusMsg(err.message || 'Search failed. Please try again.');
        setSearchingAddons(false);
      });
  };

  const handleSelectAddon = (addon) => {
    setSelectedAddon(addon);
    setSelectedAddonVersion(null);
    setAddonVersionsList([]);
    setFetchingAddonVersions(true);
    setAddonStatusMsg('');

    fetch(`${API_BASE}/servers/${selectedServer.uuid}/addons/${addon.id}/versions?source=${addonSource}&apiKey=${encodeURIComponent(curseforgeApiKey)}`, {
      headers: authHeaders
    })
      .then(res => res.json())
      .then(data => {
        setAddonVersionsList(data);
        if (data.length > 0) {
          setSelectedAddonVersion(data[0]);
        }
        setFetchingAddonVersions(false);
      })
      .catch(err => {
        console.error('Error fetching addon versions:', err);
        setFetchingAddonVersions(false);
      });
  };

  const handleInstallAddon = (version) => {
    if (!selectedServer || !selectedAddon || !version) return;
    setInstallingAddon(true);
    setAddonStatusMsg(`Downloading ${version.filename || version.name}...`);

    fetch(`${API_BASE}/servers/${selectedServer.uuid}/addons/install`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        downloadUrl: version.downloadUrl,
        filename: version.filename || `${selectedAddon.name}.jar`,
        addonName: selectedAddon.name
      })
    })
      .then(res => {
        if (!res.ok) {
          return res.json().then(errData => {
            throw new Error(errData.message || 'Download failed');
          });
        }
        return res.json();
      })
      .then(data => {
        setAddonStatusMsg(`Successfully installed ${version.filename || selectedAddon.name}!`);
        setInstallingAddon(false);
        fetchInstalledAddons();
        // Clear selected addon to reset UI
        setTimeout(() => {
          setSelectedAddon(null);
          setSelectedAddonVersion(null);
          setAddonVersionsList([]);
          setAddonStatusMsg('');
        }, 3000);
      })
      .catch(err => {
        console.error(err);
        setAddonStatusMsg(`Error: ${err.message || 'Failed to install'}`);
        setInstallingAddon(false);
      });
  };

  const handleUninstallAddon = (filename) => {
    if (!selectedServer || !filename) return;
    setUninstallingAddon(filename);

    fetch(`${API_BASE}/servers/${selectedServer.uuid}/addons/uninstall?filename=${encodeURIComponent(filename)}`, {
      method: 'DELETE',
      headers: authHeaders
    })
      .then(res => {
        if (!res.ok) {
          return res.json().then(errData => {
            throw new Error(errData.message || 'Uninstall failed');
          });
        }
        return res.json();
      })
      .then(() => {
        setUninstallingAddon(null);
        fetchInstalledAddons();
      })
      .catch(err => {
        console.error(err);
        showToast(err.message || 'Failed to uninstall');
        setUninstallingAddon(null);
      });
  };

  const fetchPlayersData = (uuid) => {
    if (!uuid) return;
    fetch(`${API_BASE}/servers/${uuid}/players`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setPlayersList(data.players || []);
        setCoordinateLogs(data.coordinate_logs || []);
        
        if (selectedPlayer) {
          const updated = (data.players || []).find(p => p.username === selectedPlayer.username);
          if (updated) {
            setSelectedPlayer(updated);
          }
        }
      })
      .catch(err => console.error(err));
  };

  const fetchSelectedPlayerData = (uuid, username) => {
    if (!uuid || !username) return;
    fetch(`${API_BASE}/servers/${uuid}/players/${username}/inventory`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setPlayerInventory(data.inventory || []);
        setPlayerEnderChest(data.ender_chest || []);
      })
      .catch(err => console.error(err));

    fetch(`${API_BASE}/servers/${uuid}/players/${username}/advancements`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setPlayerAdvancements(data || []);
      })
      .catch(err => console.error(err));

    fetch(`${API_BASE}/servers/${uuid}/players/${username}/backups`, { headers: authHeaders })
      .then(res => res.json())
      .then(data => {
        setPlayerBackups(data || []);
      })
      .catch(err => console.error(err));
  };

  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'players' && selectedServer) {
      fetchPlayersData(selectedServer.uuid);
    }
  }, [activeTab, serverTab, refreshTrigger, selectedServer]);

  // ============================================================
  // Scheduled Tasks
  // ============================================================
  const fetchScheduleTasks = () => {
    if (!selectedServer) return;
    setScheduleLoading(true);
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/schedule`, { headers: authHeaders })
      .then(r => r.json())
      .then(d => setScheduleTasks(d.tasks || []))
      .catch(err => console.error(err))
      .finally(() => setScheduleLoading(false));
  };

  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'schedule' && selectedServer) {
      fetchScheduleTasks();
    }
  }, [activeTab, serverTab, selectedServer]);

  const handleSaveScheduleTask = () => {
    if (!selectedServer || !scheduleForm) return;
    const isEdit = !!scheduleForm.id;
    const url = isEdit
      ? `${API_BASE}/servers/${selectedServer.uuid}/schedule/${scheduleForm.id}`
      : `${API_BASE}/servers/${selectedServer.uuid}/schedule`;
    const body = {
      name: scheduleForm.name,
      action_type: scheduleForm.action_type,
      payload: scheduleForm.payload,
      schedule_kind: scheduleForm.schedule_kind,
      schedule_value: scheduleForm.schedule_value,
      enabled: scheduleForm.enabled,
    };
    fetch(url, {
      method: isEdit ? 'PUT' : 'POST',
      headers: authHeaders,
      body: JSON.stringify(body),
    })
      .then(r => r.json())
      .then(d => {
        if (d.status === 'success') {
          showToast(isEdit ? 'Task updated' : 'Task created');
          setScheduleForm(null);
          fetchScheduleTasks();
        } else {
          showToast(d.message || 'Failed to save task', 'error');
        }
      })
      .catch(err => console.error(err));
  };

  const handleDeleteScheduleTask = (task) => {
    if (!selectedServer) return;
    showConfirm({
      title: 'Delete scheduled task?',
      message: `"${task.name}" will be removed permanently. Its run history is deleted as well.`,
      confirmLabel: 'Delete',
      danger: true,
      onConfirm: () => {
        fetch(`${API_BASE}/servers/${selectedServer.uuid}/schedule/${task.id}`, {
          method: 'DELETE',
          headers: authHeaders,
        })
          .then(r => r.json())
          .then(d => {
            if (d.status === 'success') { showToast('Task deleted'); fetchScheduleTasks(); }
          });
      },
    });
  };

  const handleToggleScheduleTask = (task) => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/schedule/${task.id}/toggle`, {
      method: 'POST',
      headers: authHeaders,
    })
      .then(r => r.json())
      .then(d => { if (d.status === 'success') fetchScheduleTasks(); });
  };

  const handleRunScheduleTaskNow = (task) => {
    if (!selectedServer) return;
    showToast(`Running "${task.name}"…`);
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/schedule/${task.id}/run`, {
      method: 'POST',
      headers: authHeaders,
    })
      .then(r => r.json())
      .then(d => {
        if (d.status === 'success') {
          showToast(`Task ran: ${d.result?.status || 'ok'}`);
          fetchScheduleTasks();
          if (scheduleRunsFor === task.id) fetchScheduleRuns(task.id);
        } else {
          showToast(d.message || 'Task failed', 'error');
        }
      });
  };

  const fetchScheduleRuns = (taskId) => {
    if (!selectedServer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/schedule/${taskId}/runs`, {
      headers: authHeaders,
    })
      .then(r => r.json())
      .then(d => setScheduleRuns(d.runs || []));
  };

  // ============================================================
  // Analytics
  // ============================================================
  const fetchAnalytics = () => {
    if (!selectedServer) return;
    setAnalyticsLoading(true);
    const base = `${API_BASE}/servers/${selectedServer.uuid}/analytics`;
    const opts = { headers: authHeaders };
    Promise.all([
      fetch(`${base}/summary?days=${analyticsDays}`, opts).then(r => r.json()),
      fetch(`${base}/hourly?days=${analyticsDays}`, opts).then(r => r.json()),
      fetch(`${base}/daily?days=${analyticsDays}`, opts).then(r => r.json()),
      fetch(`${base}/leaderboard?days=${analyticsDays}&limit=10`, opts).then(r => r.json()),
      fetch(`${base}/events?type=${analyticsEventType}&limit=100`, opts).then(r => r.json()),
    ])
      .then(([s, h, d, l, e]) => {
        setAnalyticsSummary(s.summary || null);
        setAnalyticsHourly(h.hourly || []);
        setAnalyticsDaily(d.daily || []);
        setAnalyticsLeaderboard(l.leaderboard || []);
        setAnalyticsEvents(e.events || []);
      })
      .catch(err => console.error(err))
      .finally(() => setAnalyticsLoading(false));
  };

  useEffect(() => {
    if (activeTab.startsWith('server-') && serverTab === 'analytics' && selectedServer) {
      fetchAnalytics();
    }
  }, [activeTab, serverTab, selectedServer, analyticsDays, analyticsEventType]);

  const formatDuration = (secs) => {
    if (!secs || secs <= 0) return '0m';
    const h = Math.floor(secs / 3600);
    const m = Math.floor((secs % 3600) / 60);
    if (h >= 24) {
      const d = Math.floor(h / 24);
      return `${d}d ${h % 24}h`;
    }
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
  };

  useEffect(() => {
    if (selectedServer && selectedPlayer) {
      fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
    }
  }, [selectedPlayer, selectedServer]);

  const handlePlayerAction = (action, extraParams = {}) => {
    if (!selectedServer || !selectedPlayer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/action`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({ action, ...extraParams })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        showToast(`Action ${action} executed successfully!`);
        fetchPlayersData(selectedServer.uuid);
        if (action === 'reset') {
          fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
        }
      } else {
        showToast(`Error executing action: ${data.message || 'unknown error'}`);
      }
    })
    .catch(err => console.error(err));
  };

  const handleGivePotion = () => {
    if (!selectedServer || !selectedPlayer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/potion`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        effect: potionEffect,
        duration: potionDuration,
        amplifier: potionAmplifier
      })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        showToast('Potion effect granted successfully!');
      } else {
        showToast('Error granting potion effect.');
      }
    })
    .catch(err => console.error(err));
  };

  const handleToggleAdvancement = (advId, currentlyGranted) => {
    if (!selectedServer || !selectedPlayer) return;
    const newGranted = currentlyGranted ? 0 : 1;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/advancements/update`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        advancement_id: advId,
        granted: newGranted
      })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
      } else {
        showToast('Error updating advancement.');
      }
    })
    .catch(err => console.error(err));
  };

  const handleCreatePlayerBackup = () => {
    if (!selectedServer || !selectedPlayer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/backup`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        backup_name: playerBackupName || 'Manual Backup'
      })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        showToast('Player backup created successfully!');
        setPlayerBackupName('');
        fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
      } else {
        showToast('Error creating player backup.');
      }
    })
    .catch(err => console.error(err));
  };

  const handleRestorePlayerBackup = (backupId) => {
    if (!selectedServer || !selectedPlayer) return;
    showConfirm({
      title: 'Restore player backup?',
      message: "This will overwrite the player's current inventory, ender chest, and stats with the snapshot.",
      danger: true,
      confirmLabel: 'Restore',
      onConfirm: () => doRestorePlayerBackup(backupId)
    });
  };

  const doRestorePlayerBackup = (backupId) => {
    if (!selectedServer || !selectedPlayer) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/backup/restore`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({ backup_id: backupId })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        showToast('Player backup restored successfully!');
        fetchPlayersData(selectedServer.uuid);
        fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
      } else {
        showToast('Error restoring backup.');
      }
    })
    .catch(err => console.error(err));
  };

  const getFirstEmptySlot = (type) => {
    const list = type === 'inventory' ? playerInventory : playerEnderChest;
    const maxSlots = type === 'inventory' ? 36 : 27;
    const occupied = new Set(list.map(item => item.slot));
    for (let i = 0; i < maxSlots; i++) {
      if (!occupied.has(i)) return i;
    }
    return -1;
  };

  const handleSaveItemEdit = () => {
    if (!selectedServer || !selectedPlayer || !selectedItemSlot) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/inventory/update`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        type: selectedItemSlot.type,
        slot: selectedItemSlot.slot,
        item_id: editItemName || 'minecraft:air',
        count: editItemCount,
        display_name: editItemName === 'minecraft:air' ? '' : editItemName,
        unbreakable: editItemUnbreakable ? 1 : 0,
        custom_aura: editItemAura,
        potion_effect: editItemPotion,
        enchants: editItemEnchants
      })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        setSelectedItemSlot(null);
        fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
      } else {
        showToast('Error saving item properties.');
      }
    })
    .catch(err => console.error(err));
  };

  const handleDeleteItem = () => {
    if (!selectedServer || !selectedPlayer || !selectedItemSlot) return;
    if (selectedItemSlot.item && selectedItemSlot.item.id) {
      fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/inventory/delete`, {
        method: 'POST',
        headers: authHeaders,
        body: JSON.stringify({ id: selectedItemSlot.item.id })
      })
      .then(res => res.json())
      .then(data => {
        if (data.status === 'success') {
          setSelectedItemSlot(null);
          fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
        } else {
          showToast('Error deleting item.');
        }
      })
      .catch(err => console.error(err));
    } else {
      setSelectedItemSlot(null);
    }
  };

  const handleDuplicateItem = () => {
    if (!selectedServer || !selectedPlayer || !selectedItemSlot) return;
    const newSlot = getFirstEmptySlot(selectedItemSlot.type);
    if (newSlot === -1) {
      showToast("No empty slots available to duplicate this item!");
      return;
    }
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/inventory/duplicate`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        type: selectedItemSlot.type,
        slot: selectedItemSlot.slot,
        new_slot: newSlot
      })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        setSelectedItemSlot(null);
        fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
      } else {
        showToast('Error duplicating item.');
      }
    })
    .catch(err => console.error(err));
  };

  const handleRepairItem = () => {
    if (!selectedServer || !selectedPlayer || !selectedItemSlot) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/inventory/repair`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        type: selectedItemSlot.type,
        slot: selectedItemSlot.slot
      })
    })
    .then(res => res.json())
    .then(data => {
      if (data.status === 'success') {
        showToast('Item repaired successfully!');
      } else {
        showToast('Error repairing item.');
      }
    })
    .catch(err => console.error(err));
  };

  const handleGiveTransferItem = () => {
    if (!selectedServer || !selectedPlayer || !selectedItemSlot || !giveTargetPlayer.trim()) return;
    fetch(`${API_BASE}/servers/${selectedServer.uuid}/players/${selectedPlayer.username}/inventory/give`, {
      method: 'POST',
      headers: authHeaders,
      body: JSON.stringify({
        target_username: giveTargetPlayer,
        type: selectedItemSlot.type,
        slot: selectedItemSlot.slot
      })
    })
    .then(res => {
      if (!res.ok) {
        return res.text().then(text => { throw new Error(text || 'Failed to transfer item') });
      }
      return res.json();
    })
    .then(data => {
      if (data.status === 'success') {
        showToast(`Transferred item to ${giveTargetPlayer}!`);
        setGiveTargetPlayer('');
        setSelectedItemSlot(null);
        fetchSelectedPlayerData(selectedServer.uuid, selectedPlayer.username);
      } else {
        showToast('Error transferring item.');
      }
    })
    .catch(err => showToast(err.message));
  };

  // ============================================================
  // Operations: import, health, automation, and maintenance
  // ============================================================
  const requestJson = async (url, options = {}) => {
    const response = await fetch(url, { ...options, headers: { ...authHeaders, ...(options.headers || {}) } });
    let data;
    try { data = await response.json(); } catch { data = { message: '' }; }
    if (!response.ok) throw new Error(data.message || `Request failed (${response.status})`);
    return data;
  };

  // ============================================================
  // Webpanel accounts and per-server access
  // ============================================================
  const loadWebpanelAccess = async () => {
    setWebpanelLoading(true);
    try {
      const [catalog, memberGroups] = await Promise.all([
        requestJson(`${API_BASE}/members/permission-catalog`),
        Promise.all(servers.map(async server => {
          try {
            const data = await requestJson(`${API_BASE}/servers/${server.uuid}/members`);
            return (data.members || []).map(member => ({
              ...member,
              server_name: server.name,
              server_software: server.software_type,
              server_version: server.version
            }));
          } catch (error) {
            console.error(`Unable to load members for ${server.name}:`, error);
            return [];
          }
        }))
      ]);
      setWebpanelCatalog({
        groups: catalog.groups || [],
        role_presets: catalog.role_presets || {}
      });
      setWebpanelMembers(memberGroups.flat());
    } catch (error) {
      showToast(error.message, { variant: 'error' });
    } finally {
      setWebpanelLoading(false);
    }
  };

  useEffect(() => {
    if (!token || activeTab !== 'webpanel') return;
    const loadTimer = window.setTimeout(loadWebpanelAccess, 0);
    return () => window.clearTimeout(loadTimer);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [token, activeTab, servers]);

  const handleInviteWebpanelMember = async (event) => {
    event.preventDefault();
    const email = webpanelInvite.email.trim().toLowerCase();
    if (!email || !webpanelInvite.server_uuid) {
      showToast('Enter an email and select a server.', { variant: 'warning' });
      return;
    }
    try {
      await requestJson(`${API_BASE}/servers/${webpanelInvite.server_uuid}/members`, {
        method: 'POST',
        body: JSON.stringify({
          email,
          display_name: webpanelInvite.display_name.trim(),
          role: webpanelInvite.role
        })
      });
      showToast(`Access assigned to ${email}. They can create their own account or use social sign-in.`, { variant: 'success' });
      setWebpanelInvite({ email: '', display_name: '', server_uuid: '', role: 'viewer' });
      loadWebpanelAccess();
    } catch (error) {
      showToast(error.message, { variant: 'error' });
    }
  };

  const updateWebpanelAssignment = async (assignment, patch, successMessage) => {
    try {
      await requestJson(`${API_BASE}/servers/${assignment.server_uuid}/members/${assignment.id}`, {
        method: 'PUT',
        body: JSON.stringify(patch)
      });
      showToast(successMessage || 'Webpanel access updated.', { variant: 'success' });
      loadWebpanelAccess();
    } catch (error) {
      showToast(error.message, { variant: 'error' });
    }
  };

  const removeWebpanelAssignment = (assignment) => showConfirm({
    title: 'Remove server access?',
    message: `${assignment.email} will no longer be able to access ${assignment.server_name}.`,
    confirmLabel: 'Remove access',
    danger: true,
    onConfirm: async () => {
      try {
        await requestJson(`${API_BASE}/servers/${assignment.server_uuid}/members/${assignment.id}`, {
          method: 'DELETE'
        });
        showToast('Server access removed.', { variant: 'success' });
        loadWebpanelAccess();
      } catch (error) {
        showToast(error.message, { variant: 'error' });
      }
    }
  });

  const openWebpanelPermissions = (assignment) => {
    setWebpanelPermissionEditor({
      ...assignment,
      permissions: { ...(assignment.permissions || {}) }
    });
  };

  const saveWebpanelPermissions = async () => {
    if (!webpanelPermissionEditor) return;
    try {
      await requestJson(`${API_BASE}/servers/${webpanelPermissionEditor.server_uuid}/members/${webpanelPermissionEditor.id}`, {
        method: 'PUT',
        body: JSON.stringify({
          role: webpanelPermissionEditor.role,
          permissions: webpanelPermissionEditor.permissions,
          display_name: webpanelPermissionEditor.display_name || ''
        })
      });
      showToast('Granular permissions saved.', { variant: 'success' });
      setWebpanelPermissionEditor(null);
      loadWebpanelAccess();
    } catch (error) {
      showToast(error.message, { variant: 'error' });
    }
  };

  const handleImportExistingServer = async () => {
    if (!importDirectory.trim()) return showToast('Enter an existing server directory.', 'error');
    setImportingServer(true);
    try {
      const data = await requestJson(`${API_BASE}/servers/import`, {
        method: 'POST',
        body: JSON.stringify({ directory: importDirectory.trim(), name: importDisplayName.trim() })
      });
      const warning = (data.warnings || []).join(' ');
      showToast(`Imported ${data.name} (${data.software_type} ${data.version}).${warning ? ` ${warning}` : ''}`);
      setImportDirectory('');
      setImportDisplayName('');
      setRefreshTrigger(value => value + 1);
      setActiveTab('overview');
    } catch (error) {
      showToast(error.message, 'error');
    } finally {
      setImportingServer(false);
    }
  };

  const fetchHealthScore = async () => {
    if (!selectedServer) return;
    setHealthLoading(true);
    try {
      const data = await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/health`);
      setHealthData(data);
    } catch (error) {
      showToast(error.message, 'error');
    } finally {
      setHealthLoading(false);
    }
  };

  const fetchAutomationRules = async () => {
    if (!selectedServer) return;
    setAutomationLoading(true);
    try {
      const data = await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/automation`);
      setAutomationRules(data.rules || []);
    } catch (error) {
      showToast(error.message, 'error');
    } finally {
      setAutomationLoading(false);
    }
  };

  const createAutomationRule = async (event) => {
    event.preventDefault();
    if (!selectedServer) return;
    try {
      await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/automation`, {
        method: 'POST', body: JSON.stringify({ ...automationForm, enabled: true })
      });
      showToast('Automation rule created.');
      fetchAutomationRules();
    } catch (error) { showToast(error.message, 'error'); }
  };

  const toggleAutomationRule = async (rule) => {
    try {
      await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/automation/${rule.id}/toggle`, { method: 'POST' });
      fetchAutomationRules();
    } catch (error) { showToast(error.message, 'error'); }
  };

  const deleteAutomationRule = (rule) => showConfirm({
    title: 'Delete automation rule?',
    message: `Delete “${rule.name}”? This stops all future evaluations for this rule.`,
    danger: true,
    confirmLabel: 'Delete rule',
    onConfirm: async () => {
      try {
        await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/automation/${rule.id}`, { method: 'DELETE' });
        fetchAutomationRules();
      } catch (error) { showToast(error.message, 'error'); }
    }
  });

  const runAutomationRule = async (rule) => {
    try {
      const data = await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/automation/${rule.id}/run`, { method: 'POST' });
      showToast(`${data.result?.status || 'done'}: ${data.result?.output || 'Rule executed.'}`);
      fetchAutomationRules();
    } catch (error) { showToast(error.message, 'error'); }
  };

  const fetchMaintenance = async () => {
    if (!selectedServer) return;
    setMaintenanceLoading(true);
    try {
      const data = await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/maintenance`);
      setMaintenance(data.maintenance || maintenance);
    } catch (error) {
      showToast(error.message, 'error');
    } finally {
      setMaintenanceLoading(false);
    }
  };

  const saveMaintenance = async () => {
    if (!selectedServer) return;
    setMaintenanceLoading(true);
    try {
      const data = await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/maintenance`, {
        method: 'PUT', body: JSON.stringify(maintenance)
      });
      setMaintenance(data.maintenance);
      showToast('Maintenance settings saved.');
    } catch (error) { showToast(error.message, 'error'); }
    finally { setMaintenanceLoading(false); }
  };

  const setMaintenanceMode = async (enabled) => {
    if (!selectedServer) return;
    setMaintenanceLoading(true);
    try {
      const endpoint = enabled ? 'enable' : 'disable';
      const data = await requestJson(`${API_BASE}/servers/${selectedServer.uuid}/maintenance/${endpoint}`, {
        method: 'POST', body: JSON.stringify(maintenance)
      });
      setMaintenance(data.maintenance);
      showToast(enabled ? 'Maintenance mode enabled.' : 'Maintenance mode disabled.');
    } catch (error) { showToast(error.message, 'error'); }
    finally { setMaintenanceLoading(false); }
  };

  useEffect(() => {
    if (!selectedServer || !activeTab.startsWith('server-') ||
        !['health', 'automation', 'maintenance'].includes(serverTab)) return undefined;
    const timer = window.setTimeout(() => {
      if (serverTab === 'health') fetchHealthScore();
      if (serverTab === 'automation') fetchAutomationRules();
      if (serverTab === 'maintenance') fetchMaintenance();
    }, 0);
    return () => window.clearTimeout(timer);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeTab, serverTab, selectedServer?.uuid]);

  const renderSlot = (type, slotIndex) => {
    const list = type === 'inventory' ? playerInventory : playerEnderChest;
    const item = list.find(it => it.slot === slotIndex);
    
    return (
      <button
        key={slotIndex}
        onClick={() => {
          setSelectedItemSlot({ type, slot: slotIndex, item });
          if (item) {
            setEditItemName(item.item_id || 'minecraft:air');
            setEditItemCount(item.count || 1);
            setEditItemUnbreakable(item.unbreakable === 1);
            setEditItemAura(item.custom_aura || '');
            setEditItemPotion(item.potion_effect || '');
            try {
              setEditItemEnchants(typeof item.enchants === 'string' ? JSON.parse(item.enchants) : item.enchants || {});
            } catch (e) {
              setEditItemEnchants(item.enchants || {});
            }
          } else {
            setEditItemName('minecraft:air');
            setEditItemCount(1);
            setEditItemUnbreakable(false);
            setEditItemAura('');
            setEditItemPotion('');
            setEditItemEnchants({});
          }
          setGiveTargetPlayer('');
        }}
        className="w-12 h-12 border border-mcdeploy-border bg-mcdeploy-bg hover:border-mcdeploy-green rounded flex items-center justify-center relative group cursor-pointer shadow-inner"
        title={item ? `${item.item_id} (Slot ${slotIndex})` : `Empty Slot ${slotIndex}`}
      >
        {item ? (
          <div className="flex flex-col items-center justify-center w-full h-full p-1">
            <span className="text-[10px] font-bold text-mcdeploy-accent truncate max-w-full">
              {item.item_id.split(':').pop().slice(0, 4).toUpperCase()}
            </span>
            {item.count > 1 && (
              <span className="absolute bottom-0.5 right-1 text-[10px] font-black text-white bg-black/60 px-1 rounded-sm">
                {item.count}
              </span>
            )}
            {item.custom_aura && (
              <span className="absolute top-0.5 left-1 w-1.5 h-1.5 rounded-full bg-purple-500 animate-ping"></span>
            )}
            {Object.keys(item.enchants || {}).length > 0 && (
              <span className="absolute top-0.5 right-1 w-1.5 h-1.5 rounded-full bg-blue-400"></span>
            )}
          </div>
        ) : (
          <span className="text-[10px] text-mcdeploy-muted/50">{slotIndex}</span>
        )}
      </button>
    );
  };

  // --- CHARTS CONFIG ---
  const chartOptions = {
    responsive: true,
    maintainAspectRatio: false,
    scales: {
      y: { min: 0, max: 100, ticks: { color: '#8e9e8e' }, grid: { color: '#242b24' } },
      x: { display: false }
    },
    plugins: { legend: { display: false } }
  };

  const cpuChartData = {
    labels: new Array(30).fill(''),
    datasets: [{
      label: 'CPU Usage (%)',
      data: historyCpu,
      borderColor: '#1ebd56',
      backgroundColor: 'rgba(30, 189, 86, 0.1)',
      fill: true,
      tension: 0.3
    }]
  };

  const ramChartData = {
    labels: new Array(30).fill(''),
    datasets: [{
      label: 'RAM Usage (%)',
      data: historyRam,
      borderColor: '#5cff96',
      backgroundColor: 'rgba(92, 255, 150, 0.1)',
      fill: true,
      tension: 0.3
    }]
  };

  const tpsChartData = {
    labels: new Array(30).fill(''),
    datasets: [{
      label: 'TPS',
      data: historyTps,
      borderColor: '#dfaf00',
      backgroundColor: 'rgba(223, 175, 0, 0.1)',
      fill: true,
      tension: 0.1
    }]
  };

  const webpanelAccounts = Object.values(webpanelMembers.reduce((accounts, assignment) => {
    const email = assignment.email.toLowerCase();
    if (!accounts[email]) {
      accounts[email] = {
        email,
        display_name: assignment.display_name || '',
        status: assignment.status,
        assignments: []
      };
    }
    if (!accounts[email].display_name && assignment.display_name) {
      accounts[email].display_name = assignment.display_name;
    }
    accounts[email].assignments.push(assignment);
    return accounts;
  }, {}))
    .filter(account => {
      const query = webpanelSearch.trim().toLowerCase();
      if (!query) return true;
      return account.email.includes(query) ||
        account.display_name.toLowerCase().includes(query) ||
        account.assignments.some(item => item.server_name.toLowerCase().includes(query));
    })
    .sort((a, b) => a.email.localeCompare(b.email));

  // Rendering Login Page
  if (!token) {
    return (
      <div className="min-h-screen bg-mcdeploy-bg flex items-center justify-center p-4">
        <div className="bg-mcdeploy-card border border-mcdeploy-border w-full max-w-md p-8 rounded-lg shadow-xl relative overflow-hidden">
          {/* Minecraft themed aesthetic header decoration */}
          <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-mcdeploy-darkgreen via-mcdeploy-green to-mcdeploy-accent"></div>
          
          <div className="flex flex-col items-center mb-8">
            {/* SVG Logo with Minecraft Green Sword */}
            <svg className="w-16 h-16 text-mcdeploy-green mb-3" fill="currentColor" viewBox="0 0 24 24">
              <path d="M19.5 2L17.5 4L9.5 12L7.5 14L4 17.5V20H6.5L10 16.5L12 14.5L20 6.5L22 4.5L19.5 2M10.5 13.5L9.5 12.5L16.5 5.5L17.5 6.5L10.5 13.5Z" />
            </svg>
            <h1 className="text-3xl font-extrabold tracking-tight text-white flex items-center gap-1">
              MC<span className="text-mcdeploy-green">Deploy</span>
            </h1>
            <p className="text-mcdeploy-muted text-sm mt-1">Minecraft Server Control Panel</p>
          </div>

          <form onSubmit={handleLogin} className="space-y-5">
            <div>
              <label className="block text-sm font-semibold text-white mb-2">Username</label>
              <input 
                type="text" 
                value={loginUser}
                onChange={(e) => setLoginUser(e.target.value)}
                required
                className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded focus:outline-none focus:border-mcdeploy-green transition duration-200" 
                placeholder="Enter admin user"
              />
            </div>
            <div>
              <label className="block text-sm font-semibold text-white mb-2">Password</label>
              <input 
                type="password" 
                value={loginPass}
                onChange={(e) => setLoginPass(e.target.value)}
                required
                className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded focus:outline-none focus:border-mcdeploy-green transition duration-200"
                placeholder="â€¢â€¢â€¢â€¢â€¢â€¢â€¢â€¢"
              />
            </div>
            
            {loginError && (
              <div className="text-red-400 text-sm bg-red-950/40 border border-red-900/50 p-3 rounded flex items-center gap-2">
                <AlertTriangle className="w-4 h-4 flex-shrink-0" />
                <span>{loginError}</span>
              </div>
            )}

            <button 
              type="submit" 
              className="w-full bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold py-3 px-4 rounded transition duration-200 flex items-center justify-center gap-2 shadow-lg shadow-mcdeploy-green/10"
            >
              Sign In <ArrowRight className="w-5 h-5" />
            </button>
          </form>

          <div className="mt-8 text-center text-xs text-mcdeploy-muted">
            Â© MCDeploy - Minecraft Server Management
          </div>
        </div>
      </div>
    );
  }

  // Sidebar content is shared between the persistent desktop sidebar and the
  // mobile drawer overlay. `pickTab` closes the drawer when a nav item is chosen
  // so the user isn't left with the overlay covering the content.
  const pickTab = (fn) => () => { fn(); setMobileNavOpen(false); };
  const SidebarContent = (
    <>
      <div>
        {/* Logo Brand Header */}
        <div className="p-5 md:p-6 border-b border-mcdeploy-border flex items-center justify-between">
          <div
            className="flex items-center gap-3 cursor-pointer"
            onClick={pickTab(() => setActiveTab('overview'))}
          >
            <svg className="w-8 h-8 text-mcdeploy-green" fill="currentColor" viewBox="0 0 24 24">
              <path d="M19.5 2L17.5 4L9.5 12L7.5 14L4 17.5V20H6.5L10 16.5L12 14.5L20 6.5L22 4.5L19.5 2M10.5 13.5L9.5 12.5L16.5 5.5L17.5 6.5L10.5 13.5Z" />
            </svg>
            <span className="text-xl font-extrabold tracking-tight text-white">
              MC<span className="text-mcdeploy-green">Deploy</span>
            </span>
          </div>
          <div className="flex items-center gap-1">
            <button
              onClick={() => setRefreshTrigger(p => p + 1)}
              className="text-mcdeploy-muted hover:text-mcdeploy-green transition p-2 -mr-2"
              title="Refresh Dashboard"
            >
              <RefreshCw className="w-4 h-4" />
            </button>
            <button
              onClick={() => setMobileNavOpen(false)}
              className="md:hidden text-mcdeploy-muted hover:text-white transition p-2 -mr-2"
              title="Close menu"
            >
              <X className="w-5 h-5" />
            </button>
          </div>
        </div>

        {/* Navigation Links */}
        <nav className="p-4 space-y-1">
          <button
            onClick={pickTab(() => { setActiveTab('overview'); setSelectedServer(null); })}
            className={`w-full flex items-center gap-3 px-4 py-3 rounded text-left font-medium transition ${activeTab === 'overview' ? 'bg-mcdeploy-green text-white shadow-md shadow-mcdeploy-green/10' : 'hover:bg-mcdeploy-border/40 text-mcdeploy-muted hover:text-white'}`}
          >
            <Activity className="w-5 h-5" /> Global Overview
          </button>

          <button
            onClick={pickTab(() => { setActiveTab('installer'); setSelectedServer(null); })}
            className={`w-full flex items-center gap-3 px-4 py-3 rounded text-left font-medium transition ${activeTab === 'installer' ? 'bg-mcdeploy-green text-white' : 'hover:bg-mcdeploy-border/40 text-mcdeploy-muted hover:text-white'}`}
          >
            <Plus className="w-5 h-5" /> Server Setup Wizard
          </button>

          <button
            onClick={pickTab(() => { setActiveTab('audit'); setSelectedServer(null); })}
            className={`w-full flex items-center gap-3 px-4 py-3 rounded text-left font-medium transition ${activeTab === 'audit' ? 'bg-mcdeploy-green text-white' : 'hover:bg-mcdeploy-border/40 text-mcdeploy-muted hover:text-white'}`}
          >
            <Shield className="w-5 h-5" /> Audit Logs
          </button>

          {role === 'admin' && (
            <button
              onClick={pickTab(() => { setActiveTab('webpanel'); setSelectedServer(null); })}
              className={`w-full flex items-center gap-3 px-4 py-3 rounded text-left font-medium transition ${activeTab === 'webpanel' ? 'bg-mcdeploy-green text-white' : 'hover:bg-mcdeploy-border/40 text-mcdeploy-muted hover:text-white'}`}
            >
              <Users className="w-5 h-5" /> Webpanel
            </button>
          )}

          <div className="pt-4 pb-2">
            <span className="px-4 text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Servers</span>
          </div>

          <div className="max-h-60 md:max-h-60 overflow-y-auto space-y-1 pr-1">
            {servers.map(s => (
              <button
                key={s.uuid}
                onClick={pickTab(() => { setSelectedServer(s); setActiveTab(`server-${s.uuid}`); })}
                className={`w-full flex items-center justify-between px-4 py-2.5 rounded text-left text-sm font-medium transition ${activeTab === `server-${s.uuid}` ? 'bg-mcdeploy-border text-white border-l-4 border-mcdeploy-green' : 'hover:bg-mcdeploy-border/20 text-mcdeploy-muted hover:text-white'}`}
              >
                <span className="truncate mr-2 flex items-center gap-2">
                  <Server className="w-4 h-4 text-mcdeploy-green flex-shrink-0" />
                  {s.name}
                </span>
                <span className={`w-2.5 h-2.5 rounded-full flex-shrink-0 ${s.status === 'Online' ? 'bg-green-500' : s.status === 'Starting' ? 'bg-yellow-500' : 'bg-red-500'}`}></span>
              </button>
            ))}
            {servers.length === 0 && (
              <div className="px-4 py-2 text-xs text-mcdeploy-muted italic">No servers configured.</div>
            )}
          </div>
        </nav>
      </div>

      {/* User Account Controls */}
      <div className="p-4 border-t border-mcdeploy-border bg-mcdeploy-bg/40 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <div className="bg-mcdeploy-green/20 border border-mcdeploy-green/30 w-8 h-8 rounded-full flex items-center justify-center text-mcdeploy-green font-bold text-sm">
            {username[0].toUpperCase()}
          </div>
          <div>
            <div className="text-sm font-semibold text-white truncate max-w-28">{username}</div>
            <div className="text-xs text-mcdeploy-muted capitalize">{role}</div>
          </div>
        </div>
      </div>
    </>
  );

  // Human-readable title for the top bar — used by both mobile and desktop headers.
  const headerTitle =
    activeTab === 'overview'  ? 'Dashboard' :
    activeTab === 'installer' ? 'Server Setup' :
    activeTab === 'audit'     ? 'Audit Logs' :
    activeTab === 'webpanel'  ? 'Webpanel Access' :
    selectedServer ? selectedServer.name : '';

  return (
    <div className="min-h-screen bg-mcdeploy-bg flex text-mcdeploy-text">

      {/* --- SIDEBAR (persistent on md+) --- */}
      <aside className="hidden md:flex w-72 bg-mcdeploy-card border-r border-mcdeploy-border flex-col justify-between flex-shrink-0">
        {SidebarContent}
      </aside>

      {/* --- MOBILE DRAWER --- */}
      {mobileNavOpen && (
        <div
          className="md:hidden fixed inset-0 z-[100] bg-black/60 animate-fade-in"
          onClick={() => setMobileNavOpen(false)}
        >
          <aside
            className="w-72 max-w-[85vw] h-full bg-mcdeploy-card border-r border-mcdeploy-border flex flex-col justify-between shadow-2xl"
            onClick={(e) => e.stopPropagation()}
          >
            {SidebarContent}
          </aside>
        </div>
      )}

      {/* --- MAIN AREA --- */}
      <main className="flex-1 flex flex-col overflow-y-auto min-w-0">

        {/* Top Navbar */}
        <header className="h-14 md:h-16 border-b border-mcdeploy-border px-3 md:px-8 flex items-center justify-between bg-mcdeploy-card sticky top-0 z-30">
          <div className="flex items-center gap-2 min-w-0">
            <button
              onClick={() => setMobileNavOpen(true)}
              className="md:hidden text-white p-2 -ml-2 rounded hover:bg-mcdeploy-border/40 transition"
              title="Open menu"
            >
              <Menu className="w-6 h-6" />
            </button>
            <h2 className="text-base md:text-xl font-bold text-white truncate">
              {headerTitle}
            </h2>
          </div>
          <div className="hidden md:block text-xs text-mcdeploy-muted font-medium bg-mcdeploy-bg border border-mcdeploy-border px-3 py-1 rounded">
            App ID: <span className="text-mcdeploy-accent">mcdeploy</span>
          </div>
          {/* Mobile-only status pill so users know the panel is live. */}
          {selectedServer && (
            <span className="md:hidden text-[10px] uppercase font-bold tracking-wider px-2 py-1 rounded bg-mcdeploy-bg border border-mcdeploy-border text-mcdeploy-muted">
              <span
                className={`inline-block w-1.5 h-1.5 rounded-full mr-1 align-middle ${
                  selectedServer.status === 'Online' ? 'bg-green-500' :
                  selectedServer.status === 'Starting' ? 'bg-yellow-500' :
                  'bg-red-500'
                }`}
              />
              {selectedServer.status}
            </span>
          )}
        </header>

        {/* --- VIEWS CONTAINER --- */}
        <div className="flex-1 p-3 md:p-6 lg:p-8">
          
          {/* ==================================== */}
          {/* VIEW: GLOBAL OVERVIEW */}
          {/* ==================================== */}
          {activeTab === 'overview' && (
            <div className="space-y-8">
              
              {/* Telemetry metrics cards */}
              <div className="grid grid-cols-1 md:grid-cols-4 gap-6">
                <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg flex items-center justify-between">
                  <div>
                    <span className="text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Servers</span>
                    <h3 className="text-3xl font-extrabold text-white mt-1">
                      {systemMetrics?.server_summary?.total || 0}
                    </h3>
                    <div className="text-xs text-mcdeploy-muted mt-2">
                      <span className="text-green-400 font-semibold">{systemMetrics?.server_summary?.online || 0} Online</span> | {systemMetrics?.server_summary?.offline || 0} Offline
                    </div>
                  </div>
                  <Server className="w-12 h-12 text-mcdeploy-green" />
                </div>

                <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg flex items-center justify-between">
                  <div>
                    <span className="text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Host CPU Average</span>
                    <h3 className="text-3xl font-extrabold text-white mt-1">
                      {systemMetrics?.host_metrics?.cpu_usage ? systemMetrics.host_metrics.cpu_usage.toFixed(1) : '0.0'}%
                    </h3>
                    <div className="w-24 bg-mcdeploy-bg h-1.5 rounded-full overflow-hidden mt-3">
                      <div className="bg-mcdeploy-green h-full" style={{ width: `${systemMetrics?.host_metrics?.cpu_usage || 0}%` }}></div>
                    </div>
                  </div>
                  <Cpu className="w-12 h-12 text-mcdeploy-green" />
                </div>

                <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg flex items-center justify-between">
                  <div>
                    <span className="text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Host RAM Usage</span>
                    <h3 className="text-3xl font-extrabold text-white mt-1 font-mono">
                      {systemMetrics?.host_metrics?.ram_used ? systemMetrics.host_metrics.ram_used.toFixed(1) : '0.0'} GB
                    </h3>
                    <div className="text-xs text-mcdeploy-muted mt-2">
                      Allocated from {systemMetrics?.host_metrics?.ram_total ? systemMetrics.host_metrics.ram_total.toFixed(0) : '0'} GB total
                    </div>
                  </div>
                  <Database className="w-12 h-12 text-mcdeploy-green" />
                </div>

                <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg flex items-center justify-between">
                  <div>
                    <span className="text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Disk Free Space</span>
                    <h3 className="text-3xl font-extrabold text-white mt-1 font-mono">
                      {systemMetrics?.host_metrics?.disk_free ? systemMetrics.host_metrics.disk_free.toFixed(0) : '0'} GB
                    </h3>
                    <div className="text-xs text-mcdeploy-muted mt-2">
                      Out of {systemMetrics?.host_metrics?.disk_total ? systemMetrics.host_metrics.disk_total.toFixed(0) : '0'} GB total capacity
                    </div>
                  </div>
                  <HardDrive className="w-12 h-12 text-mcdeploy-green" />
                </div>
              </div>

              {/* Historical Telemetry charts */}
              <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg">
                <h4 className="text-sm font-bold uppercase tracking-wider text-mcdeploy-muted mb-4">MCDeploy Analytics (Host Performance History)</h4>
                <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                  <div className="h-44">
                    <span className="text-xs font-semibold text-white">CPU Utilisation (%)</span>
                    <Line data={cpuChartData} options={chartOptions} />
                  </div>
                  <div className="h-44">
                    <span className="text-xs font-semibold text-white">RAM Utilisation (%)</span>
                    <Line data={ramChartData} options={chartOptions} />
                  </div>
                </div>
              </div>

              {/* Servers list table */}
              <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden">
                <div className="p-6 border-b border-mcdeploy-border flex items-center justify-between">
                  <h4 className="text-sm font-bold uppercase tracking-wider text-mcdeploy-muted">Server Management Overview</h4>
                  <button 
                    onClick={() => setActiveTab('installer')}
                    className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-xs font-bold py-2 px-3 rounded flex items-center gap-1.5 transition duration-150"
                  >
                    <Plus className="w-4 h-4" /> Create Server
                  </button>
                </div>
                <div className="overflow-x-auto">
                  <table className="w-full text-left border-collapse">
                    <thead>
                      <tr className="bg-mcdeploy-bg/60 text-xs font-bold text-mcdeploy-muted border-b border-mcdeploy-border">
                        <th className="p-4">Server Name</th>
                        <th className="p-4">Status</th>
                        <th className="p-4">Software / Version</th>
                        <th className="p-4">Port</th>
                        <th className="p-4">RAM Min/Max</th>
                        <th className="p-4 text-right">Actions</th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-mcdeploy-border/60">
                      {servers.map(s => (
                        <tr key={s.uuid} className="hover:bg-mcdeploy-border/10 transition duration-100 text-sm">
                          <td className="p-4 font-semibold text-white flex items-center gap-2 cursor-pointer" onClick={() => { setSelectedServer(s); setActiveTab(`server-${s.uuid}`); }}>
                            <Server className="w-4 h-4 text-mcdeploy-green flex-shrink-0" />
                            <div>
                              <div>{s.name}</div>
                              {s.subdomain && (
                                <div className="text-xs text-mcdeploy-muted font-normal font-mono mt-0.5">{s.address}</div>
                              )}
                            </div>
                          </td>
                          <td className="p-4">
                            <span className={`inline-flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-bold ${s.status === 'Online' ? 'bg-green-950/40 text-green-400 border border-green-900/50' : s.status === 'Starting' ? 'bg-yellow-950/40 text-yellow-400 border border-yellow-900/50' : 'bg-red-950/40 text-red-400 border border-red-900/50'}`}>
                              <span className={`w-1.5 h-1.5 rounded-full ${s.status === 'Online' ? 'bg-green-400' : s.status === 'Starting' ? 'bg-yellow-400' : 'bg-red-400'}`}></span>
                              {s.status}
                            </span>
                          </td>
                          <td className="p-4 text-mcdeploy-muted font-mono">{s.software_type} ({s.version})</td>
                          <td className="p-4 font-mono">{s.port}</td>
                          <td className="p-4 font-mono">{s.ram_min}M / {s.ram_max}M</td>
                          <td className="p-4 text-right">
                            <div className="flex items-center justify-end gap-1.5">
                              {s.status === 'Offline' || s.status === 'Crashed' ? (
                                <button onClick={() => triggerControl(s.uuid, 'start')} className="p-2 hover:bg-mcdeploy-border rounded text-mcdeploy-muted hover:text-green-400 transition" title="Start">
                                  <Play className="w-4 h-4" />
                                </button>
                              ) : (
                                <>
                                  <button onClick={() => triggerControl(s.uuid, 'stop')} className="p-2 hover:bg-mcdeploy-border rounded text-mcdeploy-muted hover:text-yellow-400 transition" title="Stop">
                                    <Square className="w-4 h-4" />
                                  </button>
                                  <button onClick={() => triggerControl(s.uuid, 'restart')} className="p-2 hover:bg-mcdeploy-border rounded text-mcdeploy-muted hover:text-blue-400 transition" title="Restart">
                                    <RotateCcw className="w-4 h-4" />
                                  </button>
                                </>
                              )}
                              <button onClick={() => { setSelectedServer(s); setActiveTab(`server-${s.uuid}`); }} className="bg-mcdeploy-border hover:bg-mcdeploy-green hover:text-white px-3 py-1.5 rounded text-xs transition">
                                Manage
                              </button>
                            </div>
                          </td>
                        </tr>
                      ))}
                      {servers.length === 0 && (
                        <tr>
                          <td colSpan="6" className="p-8 text-center text-mcdeploy-muted italic">No Minecraft servers installed yet. Go to the wizard to create one!</td>
                        </tr>
                      )}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* ==================================== */}
          {/* VIEW: WEBPANEL ACCESS */}
          {/* ==================================== */}
          {activeTab === 'webpanel' && role === 'admin' && (
            <div className="space-y-6">
              <div className="flex flex-col lg:flex-row lg:items-end justify-between gap-4">
                <div>
                  <div className="flex items-center gap-2 text-mcdeploy-green text-xs font-bold uppercase tracking-wider mb-2">
                    <Users className="w-4 h-4" /> Central access control
                  </div>
                  <h3 className="text-2xl font-bold text-white">Webpanel accounts</h3>
                  <p className="text-sm text-mcdeploy-muted mt-1 max-w-2xl">
                    Manage every email that can sign in to the webpanel, the servers visible to them, and the exact actions they can perform.
                  </p>
                </div>
                <button
                  onClick={loadWebpanelAccess}
                  disabled={webpanelLoading}
                  className="bg-mcdeploy-border hover:bg-mcdeploy-green text-white px-4 py-2.5 rounded flex items-center justify-center gap-2 text-sm font-semibold transition disabled:opacity-50"
                >
                  <RefreshCw className={`w-4 h-4 ${webpanelLoading ? 'animate-spin' : ''}`} /> Refresh access
                </button>
              </div>

              <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
                <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-5">
                  <div className="text-xs text-mcdeploy-muted uppercase font-bold tracking-wider">Webpanel emails</div>
                  <div className="text-3xl font-extrabold text-white mt-2">{webpanelAccounts.length}</div>
                </div>
                <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-5">
                  <div className="text-xs text-mcdeploy-muted uppercase font-bold tracking-wider">Server assignments</div>
                  <div className="text-3xl font-extrabold text-white mt-2">{webpanelMembers.length}</div>
                </div>
                <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-5">
                  <div className="text-xs text-mcdeploy-muted uppercase font-bold tracking-wider">Active access</div>
                  <div className="text-3xl font-extrabold text-mcdeploy-green mt-2">
                    {webpanelMembers.filter(item => item.status === 'active').length}
                  </div>
                </div>
              </div>

              <form onSubmit={handleInviteWebpanelMember} className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-5 md:p-6">
                <div className="flex items-center justify-between gap-4 mb-5">
                  <div className="flex items-center gap-2">
                    <UserCheck className="w-5 h-5 text-mcdeploy-green" />
                    <h4 className="font-bold text-white">Assign email access</h4>
                  </div>
                  <span className="text-xs text-mcdeploy-muted">Users create their own password or use social sign-in.</span>
                </div>
                <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-5 gap-3">
                  <input
                    type="email"
                    required
                    value={webpanelInvite.email}
                    onChange={event => setWebpanelInvite(current => ({ ...current, email: event.target.value }))}
                    placeholder="operator@example.com"
                    className="xl:col-span-2 bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded focus:outline-none focus:border-mcdeploy-green"
                  />
                  <input
                    value={webpanelInvite.display_name}
                    onChange={event => setWebpanelInvite(current => ({ ...current, display_name: event.target.value }))}
                    placeholder="Display name (optional)"
                    className="bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded focus:outline-none focus:border-mcdeploy-green"
                  />
                  <select
                    required
                    value={webpanelInvite.server_uuid}
                    onChange={event => setWebpanelInvite(current => ({ ...current, server_uuid: event.target.value }))}
                    className="bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded focus:outline-none focus:border-mcdeploy-green"
                  >
                    <option value="">Select server</option>
                    {servers.map(server => <option key={server.uuid} value={server.uuid}>{server.name}</option>)}
                  </select>
                  <div className="flex gap-2">
                    <select
                      value={webpanelInvite.role}
                      onChange={event => setWebpanelInvite(current => ({ ...current, role: event.target.value }))}
                      className="min-w-0 flex-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded focus:outline-none focus:border-mcdeploy-green"
                    >
                      <option value="viewer">Viewer</option>
                      <option value="moderator">Moderator</option>
                      <option value="admin">Admin</option>
                      <option value="owner">Owner</option>
                    </select>
                    <button type="submit" className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white px-4 py-2.5 rounded font-bold transition">
                      Add
                    </button>
                  </div>
                </div>
              </form>

              <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden">
                <div className="p-5 border-b border-mcdeploy-border flex flex-col md:flex-row md:items-center justify-between gap-3">
                  <div>
                    <h4 className="font-bold text-white">Emails and server access</h4>
                    <p className="text-xs text-mcdeploy-muted mt-1">Roles provide presets; granular permissions can override each assignment.</p>
                  </div>
                  <div className="relative w-full md:w-80">
                    <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-mcdeploy-muted" />
                    <input
                      value={webpanelSearch}
                      onChange={event => setWebpanelSearch(event.target.value)}
                      placeholder="Search email, name, or server..."
                      className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white pl-9 pr-3 py-2.5 rounded focus:outline-none focus:border-mcdeploy-green text-sm"
                    />
                  </div>
                </div>

                {webpanelLoading ? (
                  <div className="p-12 flex items-center justify-center gap-3 text-mcdeploy-muted">
                    <Loader className="w-5 h-5 animate-spin text-mcdeploy-green" /> Loading webpanel access...
                  </div>
                ) : webpanelAccounts.length === 0 ? (
                  <div className="p-12 text-center">
                    <Users className="w-10 h-10 text-mcdeploy-muted mx-auto mb-3" />
                    <h5 className="text-white font-semibold">No matching webpanel emails</h5>
                    <p className="text-sm text-mcdeploy-muted mt-1">Add an email above to grant access to its first server.</p>
                  </div>
                ) : (
                  <div className="divide-y divide-mcdeploy-border">
                    {webpanelAccounts.map(account => (
                      <section key={account.email} className="p-5 md:p-6">
                        <div className="flex flex-col md:flex-row md:items-center justify-between gap-3 mb-4">
                          <div className="flex items-center gap-3 min-w-0">
                            <div className="w-10 h-10 rounded-full bg-mcdeploy-green/15 border border-mcdeploy-green/30 text-mcdeploy-green font-bold flex items-center justify-center flex-shrink-0">
                              {(account.display_name || account.email)[0].toUpperCase()}
                            </div>
                            <div className="min-w-0">
                              <div className="font-bold text-white truncate">{account.display_name || 'Webpanel user'}</div>
                              <div className="text-sm text-mcdeploy-muted truncate">{account.email}</div>
                            </div>
                          </div>
                          <span className="text-xs font-semibold text-mcdeploy-muted bg-mcdeploy-bg border border-mcdeploy-border rounded-full px-3 py-1 self-start md:self-auto">
                            {account.assignments.length} server{account.assignments.length === 1 ? '' : 's'}
                          </span>
                        </div>

                        <div className="space-y-2">
                          {account.assignments.map(assignment => {
                            const enabledPermissions = Object.values(assignment.permissions || {}).filter(Boolean).length;
                            return (
                              <div key={assignment.id} className="bg-mcdeploy-bg/70 border border-mcdeploy-border rounded-lg p-3 flex flex-col xl:flex-row xl:items-center gap-3">
                                <div className="min-w-0 xl:flex-1">
                                  <div className="font-semibold text-white flex items-center gap-2">
                                    <Server className="w-4 h-4 text-mcdeploy-green flex-shrink-0" />
                                    <span className="truncate">{assignment.server_name}</span>
                                  </div>
                                  <div className="text-xs text-mcdeploy-muted mt-1 ml-6">
                                    {assignment.server_software} {assignment.server_version} · {enabledPermissions} permissions enabled
                                  </div>
                                </div>
                                <select
                                  value={assignment.role}
                                  onChange={event => updateWebpanelAssignment(
                                    assignment,
                                    { role: event.target.value },
                                    `${assignment.email} is now ${event.target.value} on ${assignment.server_name}.`
                                  )}
                                  className="bg-mcdeploy-card border border-mcdeploy-border text-white px-3 py-2 rounded text-sm capitalize focus:outline-none focus:border-mcdeploy-green"
                                >
                                  <option value="owner">Owner</option>
                                  <option value="admin">Admin</option>
                                  <option value="moderator">Moderator</option>
                                  <option value="viewer">Viewer</option>
                                  <option value="custom">Custom</option>
                                </select>
                                <span className={`text-xs font-bold uppercase px-2.5 py-1.5 rounded border ${assignment.status === 'active' ? 'text-green-400 border-green-900/60 bg-green-950/30' : 'text-yellow-400 border-yellow-900/60 bg-yellow-950/30'}`}>
                                  {assignment.status}
                                </span>
                                <div className="flex flex-wrap gap-2">
                                  <button
                                    onClick={() => openWebpanelPermissions(assignment)}
                                    className="px-3 py-2 rounded bg-mcdeploy-border hover:bg-mcdeploy-green text-white text-xs font-semibold transition"
                                  >
                                    Permissions
                                  </button>
                                  <button
                                    onClick={() => updateWebpanelAssignment(
                                      assignment,
                                      { status: assignment.status === 'active' ? 'suspended' : 'active' },
                                      assignment.status === 'active' ? 'Access suspended.' : 'Access reactivated.'
                                    )}
                                    className="px-3 py-2 rounded border border-mcdeploy-border hover:border-yellow-600 text-mcdeploy-muted hover:text-yellow-300 text-xs font-semibold transition"
                                  >
                                    {assignment.status === 'active' ? 'Suspend' : 'Reactivate'}
                                  </button>
                                  <button
                                    onClick={() => removeWebpanelAssignment(assignment)}
                                    className="p-2 rounded border border-red-900/50 text-red-400 hover:bg-red-950/40 transition"
                                    title={`Remove ${account.email} from ${assignment.server_name}`}
                                  >
                                    <Trash2 className="w-4 h-4" />
                                  </button>
                                </div>
                              </div>
                            );
                          })}
                        </div>
                      </section>
                    ))}
                  </div>
                )}
              </div>

              {webpanelPermissionEditor && (
                <div className="fixed inset-0 z-[120] bg-black/75 p-3 md:p-6 flex items-center justify-center" onClick={() => setWebpanelPermissionEditor(null)}>
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg shadow-2xl w-full max-w-4xl max-h-[92vh] flex flex-col" onClick={event => event.stopPropagation()}>
                    <div className="p-5 border-b border-mcdeploy-border flex items-start justify-between gap-4">
                      <div>
                        <h4 className="text-lg font-bold text-white">Granular permissions</h4>
                        <p className="text-sm text-mcdeploy-muted mt-1">
                          {webpanelPermissionEditor.email} · {webpanelPermissionEditor.server_name}
                        </p>
                      </div>
                      <button onClick={() => setWebpanelPermissionEditor(null)} className="p-2 text-mcdeploy-muted hover:text-white rounded hover:bg-mcdeploy-border">
                        <X className="w-5 h-5" />
                      </button>
                    </div>

                    <div className="p-5 border-b border-mcdeploy-border">
                      <div className="text-xs text-mcdeploy-muted font-bold uppercase tracking-wider mb-3">Apply role preset</div>
                      <div className="flex flex-wrap gap-2">
                        {['owner', 'admin', 'moderator', 'viewer'].map(preset => (
                          <button
                            key={preset}
                            onClick={() => setWebpanelPermissionEditor(current => ({
                              ...current,
                              role: preset,
                              permissions: { ...(webpanelCatalog.role_presets[preset] || {}) }
                            }))}
                            className={`px-3 py-2 rounded text-sm font-semibold capitalize border transition ${webpanelPermissionEditor.role === preset ? 'bg-mcdeploy-green border-mcdeploy-green text-white' : 'bg-mcdeploy-bg border-mcdeploy-border text-mcdeploy-muted hover:text-white'}`}
                          >
                            {preset}
                          </button>
                        ))}
                      </div>
                    </div>

                    <div className="p-5 overflow-y-auto grid grid-cols-1 md:grid-cols-2 gap-4">
                      {webpanelCatalog.groups.map(group => (
                        <fieldset key={group.label} className="bg-mcdeploy-bg/60 border border-mcdeploy-border rounded-lg p-4">
                          <legend className="px-1 text-sm font-bold text-white">{group.label}</legend>
                          <div className="space-y-3 mt-2">
                            {(group.permissions || []).map(permission => (
                              <label key={permission.key} className="flex items-start gap-3 cursor-pointer group">
                                <input
                                  type="checkbox"
                                  checked={!!webpanelPermissionEditor.permissions?.[permission.key]}
                                  onChange={event => setWebpanelPermissionEditor(current => ({
                                    ...current,
                                    role: 'custom',
                                    permissions: { ...current.permissions, [permission.key]: event.target.checked }
                                  }))}
                                  className="mt-1 accent-green-500"
                                />
                                <span>
                                  <span className="block text-sm text-white group-hover:text-mcdeploy-green transition">{permission.key}</span>
                                  <span className="block text-xs text-mcdeploy-muted mt-0.5">{permission.description}</span>
                                </span>
                              </label>
                            ))}
                          </div>
                        </fieldset>
                      ))}
                    </div>

                    <div className="p-5 border-t border-mcdeploy-border flex justify-end gap-3">
                      <button onClick={() => setWebpanelPermissionEditor(null)} className="px-4 py-2 rounded border border-mcdeploy-border text-mcdeploy-muted hover:text-white">Cancel</button>
                      <button onClick={saveWebpanelPermissions} className="px-4 py-2 rounded bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold flex items-center gap-2">
                        <Save className="w-4 h-4" /> Save permissions
                      </button>
                    </div>
                  </div>
                </div>
              )}
            </div>
          )}

          {/* ==================================== */}
          {/* VIEW: AUDIT LOGS */}
          {/* ==================================== */}
          {activeTab === 'audit' && (
            <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden space-y-4">
              <div className="p-6 border-b border-mcdeploy-border">
                <h4 className="text-sm font-bold uppercase tracking-wider text-mcdeploy-muted">MCDeploy Admin Action Audit Trail</h4>
              </div>
              <div className="overflow-x-auto">
                <table className="w-full text-left border-collapse">
                  <thead>
                    <tr className="bg-mcdeploy-bg/60 text-xs font-bold text-mcdeploy-muted border-b border-mcdeploy-border">
                      <th className="p-4">Timestamp</th>
                      <th className="p-4">User</th>
                      <th className="p-4">Action</th>
                      <th className="p-4">Server UUID</th>
                      <th className="p-4">Details</th>
                    </tr>
                  </thead>
                  <tbody className="divide-y divide-mcdeploy-border/60 font-mono text-xs">
                    {auditLogs.map((log, index) => (
                      <tr key={index} className="hover:bg-mcdeploy-border/5">
                        <td className="p-4 text-mcdeploy-muted">{log.created_at}</td>
                        <td className="p-4 text-white font-semibold">{log.username}</td>
                        <td className="p-4"><span className="px-2 py-0.5 bg-mcdeploy-border text-mcdeploy-green rounded">{log.action}</span></td>
                        <td className="p-4 text-mcdeploy-muted">{log.server_uuid || 'N/A'}</td>
                        <td className="p-4">{log.details}</td>
                      </tr>
                    ))}
                    {auditLogs.length === 0 && (
                      <tr>
                        <td colSpan="5" className="p-8 text-center text-mcdeploy-muted italic">No activity recorded yet.</td>
                      </tr>
                    )}
                  </tbody>
                </table>
              </div>
            </div>
          )}

          {/* ==================================== */}
          {/* VIEW: SERVER CREATION WIZARD */}
          {/* ==================================== */}
          {activeTab === 'installer' && (
            <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-8 max-w-3xl mx-auto relative overflow-hidden">
              <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-mcdeploy-darkgreen to-mcdeploy-green"></div>
              
              <div className="flex items-center justify-between mb-8">
                <div>
                  <h3 className="text-xl font-bold text-white">MCDeploy Minecraft Server Installer</h3>
                  <p className="text-xs text-mcdeploy-muted mt-1">Multi-step setup wizard powered by MCDeploy</p>
                </div>
                <div className="text-sm font-semibold bg-mcdeploy-border px-3 py-1.5 rounded">
                  Step {installStep} of 5
                </div>
              </div>

              {/* Progress Tracker bar */}
              <div className="w-full bg-mcdeploy-bg h-2 rounded-full overflow-hidden mb-8 flex">
                <div className="bg-mcdeploy-green h-full transition-all duration-300" style={{ width: `${(installStep / 5) * 100}%` }}></div>
              </div>

              {/* STEP 1: Basic setup */}
              {installStep === 1 && (
                <div className="space-y-6">
                  <div>
                    <label className="block text-sm font-semibold text-white mb-2">Server Name</label>
                    <input 
                      type="text" 
                      value={installName}
                      onChange={(e) => handleNameChange(e.target.value)}
                      required
                      placeholder="My Survival World"
                      className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded focus:outline-none focus:border-mcdeploy-green"
                    />
                  </div>

                  <div>
                    <label className="block text-sm font-semibold text-white mb-2">Server Address (Subdomain)</label>
                    <div className="flex rounded shadow-sm">
                      <input 
                        type="text" 
                        value={installSubdomain}
                        onChange={(e) => handleSubdomainChange(e.target.value)}
                        placeholder="myserver"
                        className="flex-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded-l focus:outline-none focus:border-mcdeploy-green font-mono text-sm"
                      />
                      <span className="inline-flex items-center px-4 rounded-r border border-l-0 border-mcdeploy-border bg-mcdeploy-border/30 text-mcdeploy-muted text-sm font-mono">
                        .mcdeploy.online
                      </span>
                    </div>
                    {installSubdomain.trim() && (
                      <div className="mt-2 text-xs flex items-center gap-1.5">
                        {subdomainStatus === 'checking' && (
                          <>
                            <Loader className="w-3.5 h-3.5 animate-spin text-yellow-500" />
                            <span className="text-yellow-500">{subdomainMessage}</span>
                          </>
                        )}
                        {subdomainStatus === 'available' && (
                          <>
                            <Check className="w-3.5 h-3.5 text-green-400" />
                            <span className="text-green-400">{subdomainMessage}</span>
                          </>
                        )}
                        {subdomainStatus === 'taken' && (
                          <>
                            <AlertTriangle className="w-3.5 h-3.5 text-red-400" />
                            <span className="text-red-400">{subdomainMessage}</span>
                          </>
                        )}
                        {subdomainStatus === 'invalid' && (
                          <>
                            <AlertTriangle className="w-3.5 h-3.5 text-red-400" />
                            <span className="text-red-400">{subdomainMessage}</span>
                          </>
                        )}
                      </div>
                    )}
                  </div>

                  <div>
                    <label className="block text-sm font-semibold text-white mb-2">Software Platform</label>
                    <select
                      value={installSoftware}
                      onChange={(e) => {
                        const val = e.target.value;
                        setInstallSoftware(val);
                        setSelectedModpack(null);
                        setSelectedModpackVersion(null);
                        setModpackSearchResults([]);
                        setInstallVersionsList([]);
                        setInstallVersion('');
                      }}
                      className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded focus:outline-none focus:border-mcdeploy-green"
                    >
                      <option value="paper">PaperMC (Recommended - optimized, plugin support)</option>
                      <option value="purpur">Purpur (Fork of Paper with custom game configurations)</option>
                      <option value="vanilla">Vanilla Minecraft (Official build - no plugins/mods)</option>
                      <option value="fabric">Fabric (Mod loader for lightweight, customizable modding)</option>
                      <option value="forge">Forge (Classic mod loader for heavy-duty modding)</option>
                      <option value="neoforge">NeoForge (Modern alternative to Forge mod loader)</option>
                      <option value="modpack">Modpack (Modrinth / CurseForge search and installation)</option>
                    </select>
                  </div>

                  {installSoftware === 'modpack' && (
                    <div className="p-5 bg-mcdeploy-card border border-mcdeploy-border rounded-lg space-y-5 shadow-inner">
                      <div className="flex items-center gap-4">
                        <span className="text-sm font-bold text-white">Repository Source:</span>
                        <div className="flex items-center gap-2">
                          <button
                            type="button"
                            onClick={() => { setModpackSource('modrinth'); setModpackSearchResults([]); setSelectedModpack(null); setSelectedModpackVersion(null); }}
                            className={`px-3 py-1.5 rounded text-xs font-bold transition duration-150 ${modpackSource === 'modrinth' ? 'bg-mcdeploy-green text-white shadow' : 'bg-mcdeploy-border/60 hover:bg-mcdeploy-border text-mcdeploy-muted hover:text-white'}`}
                          >
                            Modrinth
                          </button>
                          <button
                            type="button"
                            onClick={() => { setModpackSource('curseforge'); setModpackSearchResults([]); setSelectedModpack(null); setSelectedModpackVersion(null); }}
                            className={`px-3 py-1.5 rounded text-xs font-bold transition duration-150 ${modpackSource === 'curseforge' ? 'bg-mcdeploy-green text-white shadow' : 'bg-mcdeploy-border/60 hover:bg-mcdeploy-border text-mcdeploy-muted hover:text-white'}`}
                          >
                            CurseForge
                          </button>
                        </div>
                      </div>

                      {modpackSource === 'curseforge' && (
                        <div>
                          <label className="block text-xs font-bold text-mcdeploy-muted mb-1.5 uppercase tracking-wide">CurseForge API Key (Optional)</label>
                          <input
                            type="password"
                            value={curseforgeApiKey}
                            onChange={(e) => setCurseforgeApiKey(e.target.value)}
                            placeholder="Leave blank to use default public developer key"
                            className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded text-xs focus:outline-none focus:border-mcdeploy-green font-mono"
                          />
                        </div>
                      )}

                      <div className="flex gap-2">
                        <input
                          type="text"
                          value={modpackQuery}
                          onChange={(e) => setModpackQuery(e.target.value)}
                          onKeyDown={(e) => { if (e.key === 'Enter') { e.preventDefault(); handleSearchModpacks(); } }}
                          placeholder="Search for modpacks (e.g. Better MC, RL Craft)..."
                          className="flex-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-2.5 rounded text-sm focus:outline-none focus:border-mcdeploy-green"
                        />
                        <button
                          type="button"
                          onClick={handleSearchModpacks}
                          disabled={searchingModpacks}
                          className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white px-4 py-2.5 rounded text-sm font-bold transition duration-150 flex items-center gap-1.5 shadow-md shadow-mcdeploy-green/10"
                        >
                          {searchingModpacks ? (
                            <Loader className="w-4 h-4 animate-spin text-white" />
                          ) : (
                            <Search className="w-4 h-4 text-white" />
                          )}
                          Search
                        </button>
                        {modpackSearchResults.length > 0 && (
                          <button
                            type="button"
                            onClick={openSearchModal}
                            className="bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-white px-4 py-2.5 rounded text-sm font-bold transition duration-150 flex items-center gap-1.5"
                          >
                            Show More
                          </button>
                        )}
                      </div>

                      {modpackSearchResults.length > 0 && (
                        <div className="border border-mcdeploy-border/60 rounded-lg max-h-60 overflow-y-auto divide-y divide-mcdeploy-border/40 bg-mcdeploy-bg/40 p-1">
                          {modpackSearchResults.map(pack => (
                            <div key={pack.id} className="p-3 flex items-center justify-between gap-3 text-sm hover:bg-mcdeploy-border/20 rounded transition duration-150">
                              <div className="flex items-center gap-3 min-w-0">
                                {pack.logoUrl ? (
                                  <img src={pack.logoUrl} alt={pack.name} className="w-10 h-10 rounded object-cover flex-shrink-0 border border-mcdeploy-border" />
                                ) : (
                                  <div className="w-10 h-10 bg-mcdeploy-border/40 rounded flex items-center justify-center text-mcdeploy-muted font-bold text-xs flex-shrink-0">MP</div>
                                )}
                                <div className="min-w-0">
                                  <div className="font-bold text-white truncate">{pack.name}</div>
                                  <div className="text-xs text-mcdeploy-muted truncate">{pack.summary}</div>
                                </div>
                              </div>
                              <div className="flex items-center gap-3 flex-shrink-0">
                                <span className="text-xs text-mcdeploy-muted hidden sm:inline">
                                  {pack.downloads.toLocaleString()} downloads
                                </span>
                                <button
                                  type="button"
                                  onClick={() => handleSelectModpack(pack)}
                                  className={`px-3 py-1.5 rounded text-xs font-bold transition duration-150 ${selectedModpack?.id === pack.id ? 'bg-mcdeploy-green text-white' : 'bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-mcdeploy-muted hover:text-white'}`}
                                >
                                  {selectedModpack?.id === pack.id ? 'Selected' : 'Select'}
                                </button>
                              </div>
                            </div>
                          ))}
                        </div>
                      )}

                      {selectedModpack && (
                        <div className="bg-mcdeploy-green/10 border border-mcdeploy-green/20 p-3 rounded flex items-center gap-3">
                          <Check className="w-5 h-5 text-green-400" />
                          <div className="text-xs">
                            <span className="text-mcdeploy-muted font-semibold">Selected Modpack: </span>
                            <span className="text-white font-bold">{selectedModpack.name}</span>
                          </div>
                        </div>
                      )}
                    </div>
                  )}

                  <div>
                    <label className="block text-sm font-semibold text-white mb-2">Bind Port</label>
                    <input 
                      type="number" 
                      value={installPort}
                      onChange={(e) => setInstallPort(parseInt(e.target.value))}
                      required
                      className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded focus:outline-none focus:border-mcdeploy-green font-mono"
                    />
                  </div>

                  <div className="border border-mcdeploy-green/25 bg-mcdeploy-green/5 rounded-lg p-5 space-y-4">
                    <div className="flex items-start gap-3">
                      <Download className="w-5 h-5 text-mcdeploy-green mt-0.5" />
                      <div>
                        <h4 className="text-sm font-bold text-white">Import an existing server</h4>
                        <p className="text-xs text-mcdeploy-muted mt-1">Register a local server folder in place. MCDeploy detects its software, version, port, memory, and launch command.</p>
                      </div>
                    </div>
                    <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                      <input
                        value={importDirectory}
                        onChange={(e) => setImportDirectory(e.target.value)}
                        placeholder="C:\\Minecraft\\MyServer"
                        className="bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded text-sm font-mono focus:outline-none focus:border-mcdeploy-green"
                      />
                      <input
                        value={importDisplayName}
                        onChange={(e) => setImportDisplayName(e.target.value)}
                        placeholder="Optional display name"
                        className="bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded text-sm focus:outline-none focus:border-mcdeploy-green"
                      />
                    </div>
                    <button
                      type="button"
                      onClick={handleImportExistingServer}
                      disabled={importingServer || !importDirectory.trim()}
                      className="bg-mcdeploy-border hover:bg-mcdeploy-green disabled:opacity-50 text-white text-xs font-bold px-4 py-2.5 rounded flex items-center gap-2 transition"
                    >
                      {importingServer ? <Loader className="w-4 h-4 animate-spin" /> : <Download className="w-4 h-4" />}
                      Detect and import
                    </button>
                  </div>
                </div>
              )}

              {/* STEP 2: Version Picker */}
              {installStep === 2 && (
                <div className="space-y-6">
                  {installSoftware === 'modpack' ? (
                    <div className="space-y-4">
                      {selectedModpack ? (
                        <div className="flex items-center gap-3 p-3 bg-mcdeploy-bg/40 border border-mcdeploy-border rounded-lg">
                          {selectedModpack.logoUrl ? (
                            <img src={selectedModpack.logoUrl} alt={selectedModpack.name} className="w-12 h-12 rounded object-cover border border-mcdeploy-border" />
                          ) : (
                            <div className="w-12 h-12 bg-mcdeploy-border/60 rounded flex items-center justify-center text-mcdeploy-muted font-bold text-sm">MP</div>
                          )}
                          <div>
                            <h4 className="font-bold text-white text-sm">{selectedModpack.name}</h4>
                            <p className="text-xs text-mcdeploy-muted">Select a version compatible with your server.</p>
                          </div>
                        </div>
                      ) : (
                        <div className="p-4 bg-red-950/20 border border-red-900/30 text-red-400 text-sm rounded-lg">
                          No modpack selected! Please go back to Step 1 and select a modpack.
                        </div>
                      )}

                      <label className="block text-sm font-semibold text-white">Select Modpack Version</label>
                      <div className="space-y-2.5 max-h-72 overflow-y-auto p-1">
                        {installVersionsList.map(v => (
                          <button
                            key={v.id}
                            type="button"
                            onClick={() => {
                              setSelectedModpackVersion(v);
                              setInstallVersion(v.id);
                            }}
                            className={`w-full text-left p-3.5 rounded border transition duration-150 flex items-center justify-between gap-4 ${installVersion === v.id ? 'bg-mcdeploy-green/10 border-mcdeploy-green' : 'bg-mcdeploy-bg border-mcdeploy-border hover:bg-mcdeploy-border/40'}`}
                          >
                            <div>
                              <div className="font-bold text-white text-sm">{v.name}</div>
                              <div className="text-xs text-mcdeploy-muted mt-1 flex flex-wrap gap-2">
                                <span>Minecraft: <span className="font-semibold text-white">{v.gameVersions?.join(', ') || 'N/A'}</span></span>
                                <span>Loader: <span className="font-semibold text-white capitalize">{v.loaders?.join(', ') || 'N/A'}</span></span>
                              </div>
                            </div>
                            <div className="flex-shrink-0">
                              {v.hasServerPack ? (
                                <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded text-xs font-bold bg-green-950/40 text-green-400 border border-green-900/50">
                                  Server Pack Ready
                                </span>
                              ) : (
                                <span className="inline-flex items-center gap-1 px-2.5 py-1 rounded text-xs font-bold bg-blue-950/40 text-blue-400 border border-blue-900/50">
                                  Mods Auto-Downloaded
                                </span>
                              )}
                            </div>
                          </button>
                        ))}
                        {installVersionsList.length === 0 && (
                          <div className="py-8 flex items-center justify-center gap-2 text-mcdeploy-muted italic text-sm">
                            <Loader className="w-5 h-5 animate-spin text-mcdeploy-green" /> Fetching modpack release files...
                          </div>
                        )}
                      </div>
                    </div>
                  ) : (
                    <>
                      <label className="block text-sm font-semibold text-white">Select Minecraft Version</label>
                      <p className="text-xs text-mcdeploy-muted">Versions list is dynamically fetched from official software endpoints.</p>
                      
                      <div className="grid grid-cols-3 gap-3 max-h-72 overflow-y-auto p-1">
                        {installVersionsList.map(v => (
                          <button
                            key={v}
                            onClick={() => setInstallVersion(v)}
                            className={`p-3 rounded border text-sm font-mono text-center transition ${installVersion === v ? 'bg-mcdeploy-green border-mcdeploy-accent text-white font-bold' : 'bg-mcdeploy-bg border-mcdeploy-border hover:bg-mcdeploy-border/20 text-mcdeploy-muted hover:text-white'}`}
                          >
                            {v}
                          </button>
                        ))}
                        {installVersionsList.length === 0 && (
                          <div className="col-span-3 py-6 flex items-center justify-center gap-2 text-mcdeploy-muted italic">
                            <Loader className="w-5 h-5 animate-spin text-mcdeploy-green" /> Fetching versions manifest...
                          </div>
                        )}
                      </div>
                    </>
                  )}
                </div>
              )}

              {/* STEP 3: Resource Allocation */}
              {installStep === 3 && (
                <div className="space-y-8">
                  <div>
                    <div className="flex items-center justify-between mb-3">
                      <label className="text-sm font-semibold text-white">Maximum RAM allocation (Heap size)</label>
                      <span className="text-sm font-bold text-mcdeploy-green font-mono">{installRamMax / 1024} GB</span>
                    </div>
                    <input 
                      type="range"
                      min="1024"
                      max="16384"
                      step="512"
                      value={installRamMax}
                      onChange={(e) => {
                        const maxVal = parseInt(e.target.value);
                        setInstallRamMax(maxVal);
                        if (installRamMin > maxVal) setInstallRamMin(maxVal);
                      }}
                      className="w-full accent-mcdeploy-green bg-mcdeploy-bg rounded"
                    />
                    <div className="flex justify-between text-xs text-mcdeploy-muted mt-2">
                      <span>1 GB</span>
                      <span>2 GB (Default)</span>
                      <span>4 GB</span>
                      <span>8 GB</span>
                      <span>16 GB</span>
                    </div>
                  </div>

                  <div>
                    <div className="flex items-center justify-between mb-3">
                      <label className="text-sm font-semibold text-white">Minimum RAM allocation (Startup size)</label>
                      <span className="text-sm font-bold text-mcdeploy-green font-mono">{installRamMin / 1024} GB</span>
                    </div>
                    <input 
                      type="range"
                      min="512"
                      max={installRamMax}
                      step="512"
                      value={installRamMin}
                      onChange={(e) => setInstallRamMin(parseInt(e.target.value))}
                      className="w-full accent-mcdeploy-green bg-mcdeploy-bg rounded"
                    />
                  </div>
                </div>
              )}

              {/* STEP 4: Basic configuration setting */}
              {installStep === 4 && (
                <div className="space-y-6">
                  <div className="grid grid-cols-2 gap-6">
                    <div>
                      <label className="block text-sm font-semibold text-white mb-2">Game Difficulty</label>
                      <select
                        value={installDifficulty}
                        onChange={(e) => setInstallDifficulty(e.target.value)}
                        className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded focus:outline-none focus:border-mcdeploy-green"
                      >
                        <option value="peaceful">Peaceful</option>
                        <option value="easy">Easy</option>
                        <option value="normal">Normal</option>
                        <option value="hard">Hard</option>
                      </select>
                    </div>

                    <div>
                      <label className="block text-sm font-semibold text-white mb-2">Default Gamemode</label>
                      <select
                        value={installGamemode}
                        onChange={(e) => setInstallGamemode(e.target.value)}
                        className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-3 rounded focus:outline-none focus:border-mcdeploy-green"
                      >
                        <option value="survival">Survival</option>
                        <option value="creative">Creative</option>
                        <option value="adventure">Adventure</option>
                        <option value="spectator">Spectator</option>
                      </select>
                    </div>
                  </div>

                  <div className="space-y-4 pt-4 border-t border-mcdeploy-border">
                    <label className="flex items-center justify-between p-3 bg-mcdeploy-bg/40 border border-mcdeploy-border rounded cursor-pointer">
                      <div>
                        <div className="text-sm font-semibold text-white">Online Mode Verification</div>
                        <div className="text-xs text-mcdeploy-muted mt-0.5">Locks authentication to Mojang network servers.</div>
                      </div>
                      <input 
                        type="checkbox" 
                        checked={installOnlineMode}
                        onChange={(e) => setInstallOnlineMode(e.target.checked)}
                        className="w-5 h-5 accent-mcdeploy-green cursor-pointer"
                      />
                    </label>
                  </div>
                </div>
              )}

              {/* STEP 5: Confirm & install */}
              {installStep === 5 && (
                <div className="space-y-6">
                  <div className="bg-mcdeploy-bg border border-mcdeploy-border p-6 rounded space-y-3">
                    <h5 className="font-bold text-white border-b border-mcdeploy-border pb-2">Installation Summary</h5>
                    <div className="grid grid-cols-2 gap-2 text-sm">
                      <span className="text-mcdeploy-muted">Server Name:</span>
                      <span className="text-white font-semibold">{installName}</span>
                      <span className="text-mcdeploy-muted">Public Address:</span>
                      <span className="text-mcdeploy-green font-mono font-semibold">{installSubdomain}.mcdeploy.online</span>
                      <span className="text-mcdeploy-muted">Software Type:</span>
                      <span className="text-mcdeploy-accent uppercase font-bold">{installSoftware}</span>
                      <span className="text-mcdeploy-muted">Version Selected:</span>
                      <span className="text-white font-mono">{installVersion}</span>
                      <span className="text-mcdeploy-muted">Bind Network Port:</span>
                      <span className="text-white font-mono">{installPort}</span>
                      <span className="text-mcdeploy-muted">RAM Tuning Limits:</span>
                      <span className="text-white font-mono">{installRamMin}M - {installRamMax}M</span>
                    </div>
                  </div>

                  <div className="bg-black/40 border border-mcdeploy-border/60 p-4 rounded text-xs font-mono max-h-36 overflow-y-auto whitespace-pre-wrap">
                    {installLogs}
                  </div>

                  {installing && (
                    <div className="flex items-center gap-2 text-sm text-mcdeploy-accent font-semibold justify-center py-2">
                      <Loader className="w-5 h-5 animate-spin" /> Setup process is compiling files in background...
                    </div>
                  )}
                </div>
              )}

              {/* Navigation controls */}
              <div className="flex items-center justify-between mt-8 pt-6 border-t border-mcdeploy-border">
                {installStep > 1 && !installing ? (
                  <button 
                    onClick={() => setInstallStep(p => p - 1)}
                    className="flex items-center gap-1.5 text-sm font-semibold text-mcdeploy-muted hover:text-white px-4 py-2 border border-mcdeploy-border hover:bg-mcdeploy-border/20 rounded transition"
                  >
                    <ArrowLeft className="w-4 h-4" /> Back
                  </button>
                ) : (
                  <div></div>
                )}

                {installStep < 5 ? (
                  <button 
                    onClick={() => {
                      if (installStep === 1) {
                        if (!installName.trim()) {
                          showToast("Please specify server name");
                          return;
                        }
                        if (!installSubdomain.trim()) {
                          showToast("Please specify server subdomain address");
                          return;
                        }
                        if (subdomainStatus !== 'available') {
                          showToast(subdomainMessage || "Please enter a valid and available subdomain");
                          return;
                        }
                        if (installSoftware === 'modpack' && !selectedModpack) {
                          showToast("Please select a modpack before continuing.");
                          return;
                        }
                      }
                      if (installStep === 2) {
                        if (installSoftware === 'modpack') {
                          if (!selectedModpackVersion) {
                            showToast("Please select a modpack version.");
                            return;
                          }
                        } else {
                          if (!installVersion) {
                            showToast("Please select a Minecraft version.");
                            return;
                          }
                        }
                      }
                      setInstallStep(p => p + 1);
                    }}
                    className="flex items-center gap-1.5 text-sm font-bold bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white px-5 py-2.5 rounded transition shadow-md shadow-mcdeploy-green/10"
                  >
                    Next <ArrowRight className="w-4 h-4" />
                  </button>
                ) : (
                  !installing && (
                    <button 
                      onClick={handleInstallServer}
                      className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold px-6 py-3 rounded transition flex items-center gap-2 shadow-lg shadow-mcdeploy-green/15"
                    >
                      <Download className="w-5 h-5" /> Start Automatic Download
                    </button>
                  )
                )}
              </div>
            </div>
          )}

          {/* ==================================== */}
          {/* VIEW: PER-SERVER DEDICATED MANAGEMENT */}
          {/* ==================================== */}
          {activeTab.startsWith('server-') && selectedServer && (
            <div className="space-y-6">
              
              {/* Server Control Top Card */}
              <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg flex flex-col md:flex-row md:items-center justify-between gap-4">
                <div className="flex items-center gap-4">
                  <div className="bg-mcdeploy-green/10 border border-mcdeploy-green/20 p-3 rounded-lg text-mcdeploy-green">
                    <Server className="w-8 h-8" />
                  </div>
                  <div>
                    <h3 className="text-xl font-bold text-white flex items-center gap-2">
                      {selectedServer.name}
                      <span className="text-xs font-mono font-normal text-mcdeploy-muted">({selectedServer.software_type})</span>
                    </h3>
                    <div className="flex items-center gap-4 text-xs text-mcdeploy-muted mt-1.5">
                      <span className="flex items-center gap-1"><Cpu className="w-3.5 h-3.5" /> Core Status: 
                        <span className={`font-semibold ml-0.5 ${selectedServer.status === 'Online' ? 'text-green-400' : selectedServer.status === 'Starting' ? 'text-yellow-400' : 'text-red-400'}`}>{selectedServer.status}</span>
                      </span>
                      <span>Port: <span className="font-semibold text-white font-mono">{selectedServer.port}</span></span>
                      <span>RAM: <span className="font-semibold text-white font-mono">{selectedServer.ram_max} MB</span></span>
                    </div>
                    {selectedServer.subdomain && (
                      <div className="flex items-center gap-2.5 mt-2.5 text-xs">
                        <span className="text-mcdeploy-muted">Public Address:</span>
                        <span className="font-mono bg-mcdeploy-bg/80 px-2.5 py-1 rounded border border-mcdeploy-border text-mcdeploy-accent flex items-center gap-2">
                          {selectedServer.address}
                          <button 
                            onClick={() => {
                              navigator.clipboard.writeText(selectedServer.address);
                              showToast("Address copied to clipboard!");
                            }}
                            className="hover:text-white text-mcdeploy-muted transition"
                            title="Copy address"
                          >
                            <Copy className="w-3.5 h-3.5" />
                          </button>
                        </span>
                        <span className={`flex items-center gap-1 px-2 py-0.5 rounded text-[10px] uppercase font-bold border ${selectedServer.dns_active ? 'bg-green-500/10 text-green-400 border-green-500/20' : 'bg-red-500/10 text-red-400 border-red-500/20'}`}>
                          {selectedServer.dns_active ? 'DNS Active' : 'DNS Inactive'}
                        </span>
                      </div>
                    )}
                  </div>
                </div>

                <div className="flex items-center gap-2">
                  {selectedServer.status === 'Offline' || selectedServer.status === 'Crashed' ? (
                    <button 
                      onClick={() => triggerControl(selectedServer.uuid, 'start')}
                      className="bg-green-600 hover:bg-green-700 text-white font-bold py-2.5 px-4 rounded transition flex items-center gap-2"
                    >
                      <Play className="w-4 h-4" /> Start Server
                    </button>
                  ) : (
                    <>
                      <button 
                        onClick={() => triggerControl(selectedServer.uuid, 'stop')}
                        className="bg-yellow-600 hover:bg-yellow-700 text-white font-bold py-2.5 px-4 rounded transition flex items-center gap-2"
                      >
                        <Square className="w-4 h-4" /> Stop
                      </button>
                      <button 
                        onClick={() => triggerControl(selectedServer.uuid, 'restart')}
                        className="bg-blue-600 hover:bg-blue-700 text-white font-bold py-2.5 px-4 rounded transition flex items-center gap-2"
                      >
                        <RotateCcw className="w-4 h-4" /> Restart
                      </button>
                    </>
                  )}
                  <button 
                    onClick={() => triggerControl(selectedServer.uuid, 'kill')}
                    className="bg-red-950/60 border border-red-900/50 hover:bg-red-900 text-red-400 hover:text-white py-2.5 px-4 rounded transition flex items-center gap-2"
                  >
                    Force Kill
                  </button>
                  <button 
                    onClick={() => setShowDeleteModal(selectedServer)}
                    className="p-2.5 bg-red-950/20 hover:bg-red-900 border border-red-950 hover:border-red-800 rounded text-red-400 hover:text-white transition"
                    title="Delete Server"
                  >
                    <Trash2 className="w-5 h-5" />
                  </button>
                </div>
              </div>

              {/* Sub navigation within server */}
              <div className="border-b border-mcdeploy-border flex items-center gap-1.5 overflow-x-auto">
                {[
                  { id: 'console', label: 'Live Console', icon: Terminal },
                  { id: 'files', label: 'File Manager', icon: FileText },
                  { id: 'config', label: 'Config Editor', icon: Settings },
                  { id: 'players', label: 'Player Manager', icon: Users },
                  { id: 'analytics', label: 'Analytics', icon: BarChart3 },
                  { id: 'schedule', label: 'Scheduled Tasks', icon: Calendar },
                  { id: 'backups', label: 'Backups', icon: Database },
                  { id: 'metrics', label: 'Performance Metrics', icon: Activity },
                  { id: 'health', label: 'Health Score', icon: Shield },
                  { id: 'automation', label: 'AI Automation', icon: Zap },
                  { id: 'maintenance', label: 'Maintenance', icon: Settings },
                  { id: 'ai', label: 'AI Editor', icon: Bot },
                  ...(selectedServer && selectedServer.software_type !== 'vanilla' ? [
                    {
                      id: 'addons',
                      label: ['paper', 'purpur', 'spigot'].includes(selectedServer.software_type?.toLowerCase()) ? 'Plugin Installer' : 'Mod Installer',
                      icon: Puzzle
                    }
                  ] : [])
                ].map(tab => {
                  const Icon = tab.icon;
                  return (
                    <button
                      key={tab.id}
                      onClick={() => setServerTab(tab.id)}
                      className={`flex items-center gap-2 px-4 py-3 border-b-2 text-sm font-semibold transition whitespace-nowrap ${serverTab === tab.id ? 'border-mcdeploy-green text-white' : 'border-transparent text-mcdeploy-muted hover:text-white'}`}
                    >
                      <Icon className="w-4 h-4" /> {tab.label}
                    </button>
                  );
                })}
              </div>

              {/* --- SERVER SUB-TABS CONTENT --- */}

              {/* SUB TAB: LIVE CONSOLE */}
              {serverTab === 'console' && (
                <div className="space-y-4">
                  <div className="bg-black/90 border border-mcdeploy-border rounded-lg p-6 font-mono text-sm h-96 overflow-y-auto flex flex-col gap-1.5 shadow-inner">
                    {consoleLogs.map((log, idx) => (
                      <div key={idx} className="flex gap-2">
                        <span className="text-mcdeploy-muted font-normal flex-shrink-0">[{log.timestamp}]</span>
                        <span className={log.type === 'WARN' ? 'text-yellow-400' : log.type === 'ERROR' ? 'text-red-400' : log.type === 'INFO' ? 'text-green-400' : 'text-slate-300'}>
                          {log.text}
                        </span>
                      </div>
                    ))}
                    <div ref={consoleEndRef}></div>
                  </div>

                  <form onSubmit={handleSendCommand} className="flex items-center gap-2">
                    <input 
                      type="text" 
                      value={commandInput}
                      onChange={(e) => setCommandInput(e.target.value)}
                      placeholder="Enter minecraft console command (e.g. /help, /op username, /tps)..."
                      className="flex-1 bg-mcdeploy-card border border-mcdeploy-border px-4 py-3 rounded text-sm text-white focus:outline-none focus:border-mcdeploy-green font-mono"
                    />
                    <button type="submit" className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold py-3 px-5 rounded text-sm transition">
                      Execute
                    </button>
                  </form>
                </div>
              )}

              {/* SUB TAB: FILE MANAGER */}
              {serverTab === 'files' && (
                <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden min-h-80">
                  <div className="p-4 bg-mcdeploy-bg/40 border-b border-mcdeploy-border flex items-center justify-between">
                    <span className="text-sm font-bold text-white flex items-center gap-2">
                      <FileText className="w-4 h-4 text-mcdeploy-green" /> Server Directory Tree
                    </span>
                    {editingFile && (
                      <div className="flex items-center gap-2">
                        <span className="text-xs text-mcdeploy-muted">Editing: <span className="text-white font-mono">{editingFile}</span></span>
                        <button onClick={handleSaveFile} className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-xs font-semibold py-1.5 px-3 rounded flex items-center gap-1 transition">
                          <Save className="w-3.5 h-3.5" /> Save Changes
                        </button>
                        <button onClick={() => setEditingFile(null)} className="text-xs hover:underline text-mcdeploy-muted">Cancel</button>
                      </div>
                    )}
                  </div>

                  {editingFile ? (
                    <div className="p-4">
                      <textarea
                        value={editingContent}
                        onChange={(e) => setEditingContent(e.target.value)}
                        className="w-full h-96 bg-black text-slate-300 font-mono text-sm p-4 border border-mcdeploy-border focus:outline-none focus:border-mcdeploy-green rounded"
                      />
                    </div>
                  ) : (
                    <div className="divide-y divide-mcdeploy-border/60">
                      {filesList.map(f => (
                        <div key={f.name} className="p-3.5 flex items-center justify-between hover:bg-mcdeploy-border/10 transition text-sm">
                          <div className="flex items-center gap-2.5">
                            {f.is_directory ? (
                              <Database className="w-4 h-4 text-yellow-500" />
                            ) : (
                              <FileText className="w-4 h-4 text-slate-400" />
                            )}
                            <span className="font-medium text-white">{f.name}</span>
                            {!f.is_directory && (
                              <span className="text-xs text-mcdeploy-muted">({(f.size / 1024).toFixed(1)} KB)</span>
                            )}
                          </div>
                          
                          {!f.is_directory && (
                            <button 
                              onClick={() => handleEditFile(f.name)}
                              className="text-xs hover:text-mcdeploy-green underline flex items-center gap-1"
                            >
                              <Code className="w-3 h-3" /> Edit
                            </button>
                          )}
                        </div>
                      ))}
                      {filesList.length === 0 && (
                        <div className="p-8 text-center text-mcdeploy-muted italic">Directory is currently empty.</div>
                      )}
                    </div>
                  )}
                </div>
              )}

              {/* SUB TAB: CONFIGURATION FORM EDITOR */}
              {serverTab === 'config' && (
                <form onSubmit={handleSaveConfig} className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6 space-y-6">
                  <h4 className="text-sm font-bold uppercase tracking-wider text-mcdeploy-muted border-b border-mcdeploy-border pb-2">Properties File Configurator</h4>
                  
                  <div className="grid grid-cols-2 gap-6">
                    {Object.keys(configProperties).map(key => (
                      <div key={key} className="space-y-1.5">
                        <label className="text-xs font-bold text-white block font-mono">{key}</label>
                        <input
                          type="text"
                          value={configProperties[key]}
                          onChange={(e) => {
                            const val = e.target.value;
                            setConfigProperties(prev => ({ ...prev, [key]: val }));
                          }}
                          className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-sm focus:outline-none focus:border-mcdeploy-green font-mono"
                        />
                      </div>
                    ))}
                  </div>

                  <div className="pt-4 border-t border-mcdeploy-border flex justify-end">
                    <button type="submit" className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold py-2.5 px-5 rounded text-sm transition flex items-center gap-2">
                      <Save className="w-4 h-4" /> Save server.properties
                    </button>
                  </div>
                </form>
              )}

              {/* SUB TAB: PLAYER MANAGER */}
              {serverTab === 'players' && (
                <div className="grid grid-cols-1 lg:grid-cols-4 gap-6 min-h-[500px]">
                  
                  {/* Left Column: Player List */}
                  <div className="lg:col-span-1 bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-4 flex flex-col gap-4">
                    <div>
                      <h4 className="text-sm font-bold uppercase tracking-wider text-mcdeploy-muted mb-2">Players List</h4>
                      <p className="text-xs text-mcdeploy-muted">Manage active and historical player records.</p>
                    </div>

                    <div className="flex-1 overflow-y-auto space-y-4 max-h-[600px] pr-1">
                      {/* Online Section */}
                      <div>
                        <div className="text-xs font-bold text-green-400 uppercase tracking-wider mb-2 flex items-center gap-1.5">
                          <span className="w-2 h-2 rounded-full bg-green-400 animate-pulse"></span>
                          Online ({playersList.filter(p => p.is_online === 1 || p.is_online === true).length})
                        </div>
                        <div className="space-y-1.5">
                          {playersList.filter(p => p.is_online === 1 || p.is_online === true).map(p => (
                            <button
                              key={p.uuid}
                              onClick={() => setSelectedPlayer(p)}
                              className={`w-full flex items-center justify-between p-2.5 rounded text-left transition duration-150 ${selectedPlayer?.username === p.username ? 'bg-mcdeploy-green text-white shadow-md' : 'bg-mcdeploy-bg hover:bg-mcdeploy-border/40 text-mcdeploy-text'}`}
                            >
                              <div className="flex items-center gap-2">
                                <img
                                  src={`https://minotar.net/avatar/${p.username}/32`}
                                  alt={p.username}
                                  className="w-8 h-8 rounded bg-mcdeploy-card border border-mcdeploy-border"
                                  onError={(e) => { e.target.src = 'https://minotar.net/avatar/char/32'; }}
                                />
                                <div>
                                  <div className="text-sm font-bold">{p.username}</div>
                                  <div className="text-[10px] opacity-75 font-mono truncate max-w-[120px]">{p.uuid}</div>
                                </div>
                              </div>
                              {p.frozen === 1 && (
                                <span className="text-[10px] font-bold bg-blue-950/40 text-blue-400 border border-blue-900 px-1.5 py-0.5 rounded">FREEZE</span>
                              )}
                            </button>
                          ))}
                          {playersList.filter(p => p.is_online === 1 || p.is_online === true).length === 0 && (
                            <div className="text-xs text-mcdeploy-muted italic p-2 bg-mcdeploy-bg/30 border border-mcdeploy-border/50 rounded">No online players.</div>
                          )}
                        </div>
                      </div>

                      {/* Offline Section */}
                      <div>
                        <div className="text-xs font-bold text-mcdeploy-muted uppercase tracking-wider mb-2 flex items-center gap-1.5 border-t border-mcdeploy-border/60 pt-3">
                          <span className="w-2 h-2 rounded-full bg-mcdeploy-muted"></span>
                          Offline ({playersList.filter(p => p.is_online === 0 || p.is_online === false).length})
                        </div>
                        <div className="space-y-1.5">
                          {playersList.filter(p => p.is_online === 0 || p.is_online === false).map(p => (
                            <button
                              key={p.uuid}
                              onClick={() => setSelectedPlayer(p)}
                              className={`w-full flex items-center justify-between p-2.5 rounded text-left transition duration-150 ${selectedPlayer?.username === p.username ? 'bg-mcdeploy-green text-white shadow-md' : 'bg-mcdeploy-bg hover:bg-mcdeploy-border/40 text-mcdeploy-text'}`}
                            >
                              <div className="flex items-center gap-2">
                                <img
                                  src={`https://minotar.net/avatar/${p.username}/32`}
                                  alt={p.username}
                                  className="w-8 h-8 rounded bg-mcdeploy-card border border-mcdeploy-border"
                                  onError={(e) => { e.target.src = 'https://minotar.net/avatar/char/32'; }}
                                />
                                <div>
                                  <div className="text-sm font-bold">{p.username}</div>
                                  <div className="text-[10px] opacity-75 font-mono truncate max-w-[120px]">{p.uuid}</div>
                                </div>
                              </div>
                              {p.frozen === 1 && (
                                <span className="text-[10px] font-bold bg-blue-950/40 text-blue-400 border border-blue-900 px-1.5 py-0.5 rounded">FREEZE</span>
                              )}
                            </button>
                          ))}
                          {playersList.filter(p => p.is_online === 0 || p.is_online === false).length === 0 && (
                            <div className="text-xs text-mcdeploy-muted italic p-2 bg-mcdeploy-bg/30 border border-mcdeploy-border/50 rounded">No offline records.</div>
                          )}
                        </div>
                      </div>
                    </div>
                  </div>

                  {/* Right Column: Player Controls & Inventories */}
                  <div className="lg:col-span-3 space-y-6">
                    {selectedPlayer ? (
                      <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6 space-y-6 relative overflow-hidden">
                        
                        {/* Player General Banner */}
                        <div className="flex flex-col md:flex-row md:items-center justify-between gap-4 pb-6 border-b border-mcdeploy-border">
                          <div className="flex items-center gap-4">
                            <img
                              src={`https://minotar.net/avatar/${selectedPlayer.username}/64`}
                              alt={selectedPlayer.username}
                              className="w-16 h-16 rounded border-2 border-mcdeploy-green bg-mcdeploy-bg"
                              onError={(e) => { e.target.src = 'https://minotar.net/avatar/char/64'; }}
                            />
                            <div>
                              <h3 className="text-xl font-bold text-white flex items-center gap-2">
                                {selectedPlayer.username}
                                <span className={`w-2.5 h-2.5 rounded-full ${selectedPlayer.is_online ? 'bg-green-500' : 'bg-red-500'}`} title={selectedPlayer.is_online ? 'Online' : 'Offline'}></span>
                              </h3>
                              <div className="text-xs font-mono text-mcdeploy-muted mt-1">{selectedPlayer.uuid}</div>
                              <div className="flex gap-4 mt-2">
                                <span className="text-xs font-semibold text-red-400 bg-red-950/30 border border-red-900/50 px-2 py-0.5 rounded font-sans">
                                  â¤ Health: {selectedPlayer.health}/20
                                </span>
                                <span className="text-xs font-semibold text-amber-500 bg-amber-950/30 border border-amber-900/50 px-2 py-0.5 rounded font-sans">
                                  ðŸ– Hunger: {selectedPlayer.hunger}/20
                                </span>
                              </div>
                            </div>
                          </div>

                          <div className="flex flex-wrap items-center gap-1.5 font-sans">
                            <button
                              onClick={() => handlePlayerAction('heal')}
                              className="bg-red-900/40 hover:bg-red-900 text-red-300 hover:text-white border border-red-800 text-xs font-bold py-1.5 px-3 rounded transition"
                              title="Heal player to full health"
                            >
                              Heal
                            </button>
                            <button
                              onClick={() => handlePlayerAction('feed')}
                              className="bg-amber-900/40 hover:bg-amber-900 text-amber-300 hover:text-white border border-amber-800 text-xs font-bold py-1.5 px-3 rounded transition"
                              title="Feed player to full hunger"
                            >
                              Feed
                            </button>
                            {selectedPlayer.frozen === 1 ? (
                              <button
                                onClick={() => handlePlayerAction('unfreeze')}
                                className="bg-blue-900 hover:bg-blue-800 text-white text-xs font-bold py-1.5 px-3 rounded transition"
                              >
                                Unfreeze
                              </button>
                            ) : (
                              <button
                                onClick={() => handlePlayerAction('freeze')}
                                className="bg-blue-900/40 hover:bg-blue-900 text-blue-300 hover:text-white border border-blue-800 text-xs font-bold py-1.5 px-3 rounded transition"
                              >
                                Freeze
                              </button>
                            )}
                            <button
                              onClick={() => { setShowActionModal('kick'); setActionReason(''); }}
                              className="bg-orange-950/40 hover:bg-orange-900 text-orange-400 hover:text-white border border-orange-900 text-xs font-bold py-1.5 px-3 rounded transition"
                            >
                              Kick
                            </button>
                            <button
                              onClick={() => { setShowActionModal('ban'); setActionReason(''); }}
                              className="bg-red-950/60 hover:bg-red-950 border border-red-900 text-red-400 hover:text-white text-xs font-bold py-1.5 px-3 rounded transition"
                            >
                              Ban
                            </button>
                            <button
                              onClick={() => { setShowActionModal('tempban'); setActionReason(''); setActionDuration('1h'); }}
                              className="bg-rose-950/60 hover:bg-rose-950 border border-rose-900 text-rose-400 hover:text-white text-xs font-bold py-1.5 px-3 rounded transition"
                            >
                              Temp Ban
                            </button>
                            <button
                              onClick={() => { setShowActionModal('timeout'); setActionDuration('10m'); }}
                              className="bg-purple-950/60 hover:bg-purple-950 border border-purple-900 text-purple-400 hover:text-white text-xs font-bold py-1.5 px-3 rounded transition"
                            >
                              Mute/Timeout
                            </button>
                            <button
                              onClick={() => {
                                showConfirm({
                                  title: 'Wipe this player entirely?',
                                  message: "This will erase the player's inventory, ender chest, stats, backups, and advancements from the database. This action cannot be undone.",
                                  danger: true,
                                  confirmLabel: 'Reset player',
                                  onConfirm: () => handlePlayerAction('reset')
                                });
                              }}
                              className="bg-red-700 hover:bg-red-800 text-white text-xs font-bold py-1.5 px-3 rounded transition"
                            >
                              Reset Player
                            </button>
                          </div>
                        </div>

                        {/* Inline Actions Modal */}
                        {showActionModal && (
                          <div className="bg-mcdeploy-bg border border-mcdeploy-border p-4 rounded space-y-3 font-sans">
                            <h5 className="text-sm font-bold text-white capitalize">Execute {showActionModal} on {selectedPlayer.username}</h5>
                            <div className="flex flex-col md:flex-row gap-3">
                              {showActionModal !== 'timeout' && (
                                <input
                                  type="text"
                                  placeholder="Reason for action..."
                                  value={actionReason}
                                  onChange={(e) => setActionReason(e.target.value)}
                                  className="flex-1 bg-mcdeploy-card border border-mcdeploy-border px-3 py-2 rounded text-xs text-white focus:outline-none focus:border-mcdeploy-green"
                                />
                              )}
                              {(showActionModal === 'tempban' || showActionModal === 'timeout') && (
                                <input
                                  type="text"
                                  placeholder="Duration (e.g. 10m, 1h, 7d)..."
                                  value={actionDuration}
                                  onChange={(e) => setActionDuration(e.target.value)}
                                  className="w-48 bg-mcdeploy-card border border-mcdeploy-border px-3 py-2 rounded text-xs text-white focus:outline-none focus:border-mcdeploy-green font-mono"
                                />
                              )}
                              <div className="flex gap-2">
                                <button
                                  type="button"
                                  onClick={() => {
                                    if (showActionModal === 'kick') {
                                      handlePlayerAction('kick', { reason: actionReason });
                                    } else if (showActionModal === 'ban') {
                                      handlePlayerAction('ban', { reason: actionReason });
                                    } else if (showActionModal === 'tempban') {
                                      handlePlayerAction('tempban', { reason: actionReason, duration: actionDuration });
                                    } else if (showActionModal === 'timeout') {
                                      handlePlayerAction('timeout', { duration: actionDuration });
                                    }
                                    setShowActionModal(null);
                                  }}
                                  className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-xs font-bold px-4 py-2 rounded"
                                >
                                  Confirm
                                </button>
                                <button
                                  type="button"
                                  onClick={() => setShowActionModal(null)}
                                  className="text-xs hover:underline text-mcdeploy-muted px-2"
                                >
                                  Cancel
                                </button>
                              </div>
                            </div>
                          </div>
                        )}

                        {/* Player Detailed Tabs */}
                        <div className="border-b border-mcdeploy-border flex items-center gap-1.5 font-sans">
                          {[
                            { id: 'inventory', label: 'Inventory Grid' },
                            { id: 'ender_chest', label: 'Ender Chest' },
                            { id: 'potion', label: 'Potion Effects' },
                            { id: 'advancements', label: 'Advancements' },
                            { id: 'backups', label: 'Backup & Restore' },
                            { id: 'coordinates', label: 'Coordinate Logs' }
                          ].map(t => (
                            <button
                              key={t.id}
                              onClick={() => setPlayerDetailTab(t.id)}
                              className={`px-3 py-2 border-b-2 text-xs font-semibold transition ${playerDetailTab === t.id ? 'border-mcdeploy-green text-white' : 'border-transparent text-mcdeploy-muted hover:text-white'}`}
                            >
                              {t.label}
                            </button>
                          ))}
                        </div>

                        {/* Tab Content: Inventory Grid */}
                        {playerDetailTab === 'inventory' && (
                          <div className="space-y-4">
                            <div>
                              <h4 className="text-sm font-bold uppercase tracking-wider text-white">Survival Inventory Editor</h4>
                              <p className="text-xs text-mcdeploy-muted mt-1">Lays out slots 9-35 (main inventory) and slots 0-8 (hotbar). Click any cell to customize.</p>
                            </div>
                            
                            <div className="bg-mcdeploy-bg/40 border border-mcdeploy-border p-4 rounded-lg flex flex-col gap-6 max-w-lg mx-auto">
                              <div className="grid grid-cols-9 gap-1.5 justify-center">
                                {Array.from({ length: 27 }, (_, i) => i + 9).map(sIndex => renderSlot('inventory', sIndex))}
                              </div>
                              <div className="border-t border-mcdeploy-border/60"></div>
                              <div className="grid grid-cols-9 gap-1.5 justify-center">
                                {Array.from({ length: 9 }, (_, i) => i).map(sIndex => renderSlot('inventory', sIndex))}
                              </div>
                            </div>
                          </div>
                        )}

                        {/* Tab Content: Ender Chest */}
                        {playerDetailTab === 'ender_chest' && (
                          <div className="space-y-4">
                            <div>
                              <h4 className="text-sm font-bold uppercase tracking-wider text-white">Ender Chest Editor</h4>
                              <p className="text-xs text-mcdeploy-muted mt-1">Lays out slots 0-26 of the player's private Ender Chest vault. Click any cell to customize.</p>
                            </div>
                            
                            <div className="bg-mcdeploy-bg/40 border border-mcdeploy-border p-4 rounded-lg flex justify-center max-w-lg mx-auto">
                              <div className="grid grid-cols-9 gap-1.5">
                                {Array.from({ length: 27 }, (_, i) => i).map(sIndex => renderSlot('ender_chest', sIndex))}
                              </div>
                            </div>
                          </div>
                        )}

                        {/* Tab Content: Potion Effects */}
                        {playerDetailTab === 'potion' && (
                          <div className="space-y-6 max-w-xl">
                            <div>
                              <h4 className="text-sm font-bold uppercase tracking-wider text-white font-mono">Grant Potion Effect Wizard</h4>
                              <p className="text-xs text-mcdeploy-muted mt-1 font-sans">Apply customized runtime potion modifiers directly to the player.</p>
                            </div>
                            
                            <div className="space-y-4 bg-mcdeploy-bg/40 border border-mcdeploy-border p-5 rounded-lg font-sans">
                              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                                <div>
                                  <label className="block text-xs font-bold text-white mb-2 font-mono">Effect Key</label>
                                  <select
                                    value={potionEffect}
                                    onChange={(e) => setPotionEffect(e.target.value)}
                                    className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-xs focus:outline-none focus:border-mcdeploy-green"
                                  >
                                    <option value="minecraft:speed">Speed</option>
                                    <option value="minecraft:slowness">Slowness</option>
                                    <option value="minecraft:haste">Haste</option>
                                    <option value="minecraft:strength">Strength</option>
                                    <option value="minecraft:instant_health">Instant Health</option>
                                    <option value="minecraft:instant_damage">Instant Damage</option>
                                    <option value="minecraft:jump_boost">Jump Boost</option>
                                    <option value="minecraft:regeneration">Regeneration</option>
                                    <option value="minecraft:resistance">Resistance</option>
                                    <option value="minecraft:fire_resistance">Fire Resistance</option>
                                    <option value="minecraft:water_breathing">Water Breathing</option>
                                    <option value="minecraft:invisibility">Invisibility</option>
                                    <option value="minecraft:night_vision">Night Vision</option>
                                    <option value="minecraft:hunger">Hunger</option>
                                    <option value="minecraft:weakness">Weakness</option>
                                    <option value="minecraft:poison">Poison</option>
                                    <option value="minecraft:wither">Wither</option>
                                    <option value="minecraft:health_boost">Health Boost</option>
                                    <option value="minecraft:absorption">Absorption</option>
                                    <option value="minecraft:saturation">Saturation</option>
                                    <option value="minecraft:glowing">Glowing</option>
                                    <option value="minecraft:levitation">Levitation</option>
                                  </select>
                                </div>
                                <div className="grid grid-cols-2 gap-3">
                                  <div>
                                    <label className="block text-xs font-bold text-white mb-2 font-mono">Duration (seconds)</label>
                                    <input
                                      type="number"
                                      value={potionDuration}
                                      onChange={(e) => setPotionDuration(parseInt(e.target.value) || 30)}
                                      className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-xs focus:outline-none focus:border-mcdeploy-green font-mono"
                                    />
                                  </div>
                                  <div>
                                    <label className="block text-xs font-bold text-white mb-2 font-mono">Amplifier (level)</label>
                                    <input
                                      type="number"
                                      value={potionAmplifier}
                                      onChange={(e) => setPotionAmplifier(parseInt(e.target.value) || 1)}
                                      className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-xs focus:outline-none focus:border-mcdeploy-green font-mono"
                                    />
                                  </div>
                                </div>
                              </div>

                              <button
                                onClick={handleGivePotion}
                                className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-xs font-bold py-2.5 px-4 rounded transition w-full"
                              >
                                Apply Potion Effect to {selectedPlayer.username}
                              </button>
                            </div>
                          </div>
                        )}

                        {/* Tab Content: Advancements */}
                        {playerDetailTab === 'advancements' && (
                          <div className="space-y-4">
                            <div>
                              <h4 className="text-sm font-bold uppercase tracking-wider text-white">Advancements & Achievements</h4>
                              <p className="text-xs text-mcdeploy-muted mt-1">Check to grant or uncheck to revoke advancements for the player.</p>
                            </div>
                            
                            <div className="grid grid-cols-1 md:grid-cols-2 gap-4 max-h-96 overflow-y-auto pr-1">
                              {playerAdvancements.map(adv => (
                                <label
                                  key={adv.id}
                                  className="flex items-start justify-between p-3 bg-mcdeploy-bg/40 border border-mcdeploy-border rounded cursor-pointer hover:border-mcdeploy-green/60 transition font-sans"
                                >
                                  <div>
                                    <div className="text-xs font-bold text-white flex items-center gap-1.5 font-mono">
                                      <span className="text-mcdeploy-green">minecraft:{adv.id}</span>
                                      {adv.title && <span className="font-sans font-bold">({adv.title})</span>}
                                    </div>
                                    <div className="text-[10px] text-mcdeploy-muted mt-0.5">{adv.description}</div>
                                  </div>
                                  <input
                                    type="checkbox"
                                    checked={adv.granted === 1}
                                    onChange={() => handleToggleAdvancement(adv.id, adv.granted === 1)}
                                    className="w-4 h-4 accent-mcdeploy-green mt-0.5 cursor-pointer"
                                  />
                                </label>
                              ))}
                              {playerAdvancements.length === 0 && (
                                <div className="col-span-2 text-xs text-mcdeploy-muted italic p-4 text-center">No advancements loaded.</div>
                              )}
                            </div>
                          </div>
                        )}

                        {/* Tab Content: Backup & Restore */}
                        {playerDetailTab === 'backups' && (
                          <div className="space-y-6">
                            <div className="bg-mcdeploy-bg/40 border border-mcdeploy-border p-5 rounded-lg flex flex-col md:flex-row md:items-end gap-4 justify-between font-mono">
                              <div className="flex-1 space-y-1.5 font-sans">
                                <label className="block text-xs font-bold text-white font-mono">Create Player Backup Snapshot</label>
                                <input
                                  type="text"
                                  value={playerBackupName}
                                  onChange={(e) => setPlayerBackupName(e.target.value)}
                                  placeholder="E.g. Before Diamond Sword upgrade..."
                                  className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-xs focus:outline-none focus:border-mcdeploy-green font-sans"
                                />
                              </div>
                              <button
                                onClick={handleCreatePlayerBackup}
                                className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-xs font-bold py-2.5 px-5 rounded transition font-sans"
                              >
                                Snapshot Now
                              </button>
                            </div>

                            <div className="border border-mcdeploy-border rounded-lg overflow-hidden bg-mcdeploy-bg/20">
                              <table className="w-full text-left border-collapse">
                                <thead>
                                  <tr className="bg-mcdeploy-bg/60 text-xs font-bold text-mcdeploy-muted border-b border-mcdeploy-border">
                                    <th className="p-3">Snapshot Name</th>
                                    <th className="p-3">Backup ID</th>
                                    <th className="p-3">Created At</th>
                                    <th className="p-3 text-right">Actions</th>
                                  </tr>
                                </thead>
                                <tbody className="divide-y divide-mcdeploy-border/60 text-xs font-mono">
                                  {playerBackups.map(b => (
                                    <tr key={b.backup_id} className="hover:bg-mcdeploy-border/5">
                                      <td className="p-3 text-white font-sans font-semibold">{b.backup_name}</td>
                                      <td className="p-3 text-mcdeploy-muted truncate max-w-[120px]">{b.backup_id}</td>
                                      <td className="p-3 text-mcdeploy-muted">{b.created_at}</td>
                                      <td className="p-3 text-right">
                                        <button
                                          onClick={() => handleRestorePlayerBackup(b.backup_id)}
                                          className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-[10px] font-bold py-1.5 px-3 rounded transition font-sans"
                                        >
                                          Restore Snapshot
                                        </button>
                                      </td>
                                    </tr>
                                  ))}
                                  {playerBackups.length === 0 && (
                                    <tr>
                                      <td colSpan="4" className="p-6 text-center text-mcdeploy-muted italic text-xs font-sans">No backups created for {selectedPlayer.username} yet.</td>
                                    </tr>
                                  )}
                                </tbody>
                              </table>
                            </div>
                          </div>
                        )}

                        {/* Tab Content: Coordinate Logs */}
                        {playerDetailTab === 'coordinates' && (
                          <div className="space-y-4">
                            <div>
                              <h4 className="text-sm font-bold uppercase tracking-wider text-white">Login / Logoff Coordinates History</h4>
                              <p className="text-xs text-mcdeploy-muted mt-1">Logs coordinates of player join/leave server actions parsed from console outputs.</p>
                            </div>

                            <div className="border border-mcdeploy-border rounded-lg overflow-hidden bg-mcdeploy-bg/20 font-sans">
                              <table className="w-full text-left border-collapse">
                                <thead>
                                  <tr className="bg-mcdeploy-bg/60 text-xs font-bold text-mcdeploy-muted border-b border-mcdeploy-border">
                                    <th className="p-3">Timestamp</th>
                                    <th className="p-3 font-sans">Type</th>
                                    <th className="p-3 font-mono">Coordinates (X, Y, Z)</th>
                                  </tr>
                                </thead>
                                <tbody className="divide-y divide-mcdeploy-border/60 text-xs font-mono">
                                  {coordinateLogs
                                    .filter(c => c.player_uuid === selectedPlayer.uuid || c.username === selectedPlayer.username)
                                    .map(c => (
                                      <tr key={c.id} className="hover:bg-mcdeploy-border/5">
                                        <td className="p-3 text-mcdeploy-muted">{c.timestamp}</td>
                                        <td className="p-3">
                                          <span className={`inline-flex px-2 py-0.5 rounded text-[10px] font-bold font-sans ${c.type === 'login' ? 'bg-green-950/40 text-green-400 border border-green-900/50' : 'bg-red-950/40 text-red-400 border border-red-900/50'}`}>
                                            {c.type.toUpperCase()}
                                          </span>
                                        </td>
                                        <td className="p-3 text-white font-bold">
                                          X: {c.x.toFixed(1)} | Y: {c.y.toFixed(1)} | Z: {c.z.toFixed(1)}
                                        </td>
                                      </tr>
                                    ))}
                                  {coordinateLogs.filter(c => c.player_uuid === selectedPlayer.uuid || c.username === selectedPlayer.username).length === 0 && (
                                    <tr>
                                      <td colSpan="3" className="p-6 text-center text-mcdeploy-muted italic text-xs font-sans">No coordinate logs recorded for {selectedPlayer.username} yet.</td>
                                    </tr>
                                  )}
                                </tbody>
                              </table>
                            </div>
                          </div>
                        )}

                      </div>
                    ) : (
                      <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-12 text-center flex flex-col items-center justify-center gap-3">
                        <Users className="w-12 h-12 text-mcdeploy-muted animate-pulse" />
                        <h4 className="text-sm font-bold uppercase tracking-wider text-white">No Player Selected</h4>
                        <p className="text-xs text-mcdeploy-muted max-w-xs font-sans">Select a player from the list on the left to start modifying inventories, ender chests, advancements, and snapshots.</p>
                      </div>
                    )}
                  </div>

                  {/* Slot Item Properties Modal */}
                  {selectedItemSlot && (
                    <div className="fixed inset-0 z-50 bg-black/75 flex items-center justify-center p-4">
                      <div className="bg-mcdeploy-card border border-mcdeploy-border w-full max-w-xl p-6 rounded-lg shadow-2xl relative space-y-6">
                        
                        <div className="flex items-center justify-between border-b border-mcdeploy-border pb-3">
                          <h4 className="text-sm font-bold uppercase tracking-wider text-white">
                            Modify Slot: {selectedItemSlot.type.toUpperCase()} #{selectedItemSlot.slot}
                          </h4>
                          <button
                            onClick={() => setSelectedItemSlot(null)}
                            className="text-mcdeploy-muted hover:text-white text-sm"
                          >
                            âœ•
                          </button>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs font-sans">
                          {/* Left Panel: Properties */}
                          <div className="space-y-4 font-sans">
                            <div>
                              <label className="block text-xs font-bold text-white mb-1.5 font-mono">Item ID</label>
                              <input
                                list="common-items"
                                type="text"
                                value={editItemName}
                                onChange={(e) => setEditItemName(e.target.value)}
                                className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded focus:outline-none focus:border-mcdeploy-green font-mono"
                              />
                              <datalist id="common-items">
                                {COMMON_ITEMS.map(i => <option key={i} value={i} />)}
                              </datalist>
                            </div>

                            <div className="grid grid-cols-2 gap-3">
                              <div>
                                <label className="block text-xs font-bold text-white mb-1.5 font-mono">Count</label>
                                <input
                                  type="number"
                                  min="1"
                                  max="64"
                                  value={editItemCount}
                                  onChange={(e) => setEditItemCount(parseInt(e.target.value) || 1)}
                                  className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded focus:outline-none focus:border-mcdeploy-green font-mono"
                                />
                              </div>
                              <div className="flex items-end h-full font-sans">
                                <label className="flex items-center gap-2 cursor-pointer py-2">
                                  <input
                                    type="checkbox"
                                    checked={editItemUnbreakable}
                                    onChange={(e) => setEditItemUnbreakable(e.target.checked)}
                                    className="w-4 h-4 accent-mcdeploy-green cursor-pointer"
                                  />
                                  <span className="font-bold text-white">Unbreakable</span>
                                </label>
                              </div>
                            </div>

                            <div>
                              <label className="block text-xs font-bold text-white mb-1.5 font-mono">Custom Aura Data</label>
                              <input
                                type="text"
                                value={editItemAura}
                                onChange={(e) => setEditItemAura(e.target.value)}
                                placeholder="E.g. glowing, fiery, frost..."
                                className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded focus:outline-none focus:border-mcdeploy-green"
                              />
                            </div>

                            <div>
                              <label className="block text-xs font-bold text-white mb-1.5 font-mono">Potion Effects (JSON/Text)</label>
                              <input
                                type="text"
                                value={editItemPotion}
                                onChange={(e) => setEditItemPotion(e.target.value)}
                                placeholder="E.g. regeneration, speed..."
                                className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded focus:outline-none focus:border-mcdeploy-green"
                              />
                            </div>
                          </div>

                          {/* Right Panel: Enchantments & Give/Transfer */}
                          <div className="space-y-4 font-sans">
                            <div>
                              <label className="block text-xs font-bold text-white mb-1.5 font-mono">Enchantments List</label>
                              <div className="border border-mcdeploy-border rounded bg-mcdeploy-bg/40 p-2 space-y-1.5 max-h-36 overflow-y-auto">
                                {Object.entries(editItemEnchants || {}).map(([enchant, level]) => (
                                  <div key={enchant} className="flex items-center justify-between bg-mcdeploy-bg p-1.5 border border-mcdeploy-border/60 rounded text-[10px] font-mono">
                                    <span className="truncate max-w-[120px]"><span className="text-mcdeploy-green">{enchant.replace('minecraft:', '')}</span> (Lv {level})</span>
                                    <button
                                      type="button"
                                      onClick={() => {
                                        const copy = { ...editItemEnchants };
                                        delete copy[enchant];
                                        setEditItemEnchants(copy);
                                      }}
                                      className="text-red-400 hover:text-red-300 font-bold font-sans"
                                    >
                                      Remove
                                    </button>
                                  </div>
                                ))}
                                {Object.keys(editItemEnchants || {}).length === 0 && (
                                  <div className="text-[10px] text-mcdeploy-muted italic font-sans">No enchantments.</div>
                                )}
                              </div>
                            </div>

                            <div className="flex gap-1.5 items-center font-mono">
                              <select
                                value={newEnchantName}
                                onChange={(e) => setNewEnchantName(e.target.value)}
                                className="bg-mcdeploy-bg border border-mcdeploy-border text-white text-[10px] p-1.5 rounded focus:outline-none focus:border-mcdeploy-green flex-1"
                              >
                                <option value="minecraft:sharpness">Sharpness</option>
                                <option value="minecraft:smite">Smite</option>
                                <option value="minecraft:fire_aspect">Fire Aspect</option>
                                <option value="minecraft:knockback">Knockback</option>
                                <option value="minecraft:looting">Looting</option>
                                <option value="minecraft:protection">Protection</option>
                                <option value="minecraft:fire_protection">Fire Protection</option>
                                <option value="minecraft:feather_falling">Feather Falling</option>
                                <option value="minecraft:blast_protection">Blast Protection</option>
                                <option value="minecraft:projectile_protection">Projectile Protection</option>
                                <option value="minecraft:respiration">Respiration</option>
                                <option value="minecraft:thorns">Thorns</option>
                                <option value="minecraft:efficiency">Efficiency</option>
                                <option value="minecraft:silk_touch">Silk Touch</option>
                                <option value="minecraft:unbreaking">Unbreaking</option>
                                <option value="minecraft:fortune">Fortune</option>
                                <option value="minecraft:power">Power</option>
                                <option value="minecraft:punch">Punch</option>
                                <option value="minecraft:flame">Flame</option>
                                <option value="minecraft:infinity">Infinity</option>
                                <option value="minecraft:luck_of_the_sea">Luck of the Sea</option>
                                <option value="minecraft:lure">Lure</option>
                                <option value="minecraft:mending">Mending</option>
                              </select>
                              <input
                                type="number"
                                min="1"
                                max="255"
                                value={newEnchantLevel}
                                onChange={(e) => setNewEnchantLevel(parseInt(e.target.value) || 1)}
                                className="w-12 bg-mcdeploy-bg border border-mcdeploy-border text-white text-[10px] p-1.5 rounded focus:outline-none focus:border-mcdeploy-green font-mono"
                              />
                              <button
                                type="button"
                                onClick={() => {
                                  setEditItemEnchants(prev => ({
                                    ...prev,
                                    [newEnchantName]: newEnchantLevel
                                  }));
                                }}
                                className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-[10px] font-bold px-2 py-1.5 rounded font-sans"
                              >
                                Add
                              </button>
                            </div>

                            {selectedItemSlot.item && (
                              <div className="border-t border-mcdeploy-border pt-3 space-y-2 font-sans">
                                <label className="block text-xs font-bold text-white font-mono">Transfer / Give Item</label>
                                <div className="flex gap-1.5">
                                  <input
                                    type="text"
                                    value={giveTargetPlayer}
                                    onChange={(e) => setGiveTargetPlayer(e.target.value)}
                                    placeholder="Target username..."
                                    className="flex-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-2 py-1.5 rounded focus:outline-none focus:border-mcdeploy-green font-sans"
                                  />
                                  <button
                                    type="button"
                                    onClick={handleGiveTransferItem}
                                    className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold px-3 py-1.5 rounded text-[10px] font-sans"
                                  >
                                    Transfer
                                  </button>
                                </div>
                              </div>
                            )}
                          </div>
                        </div>

                        {/* Modal Action Footer buttons */}
                        <div className="border-t border-mcdeploy-border pt-4 flex flex-wrap items-center justify-between gap-3 font-sans">
                          <div className="flex gap-2">
                            <button
                              type="button"
                              onClick={handleSaveItemEdit}
                              className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-xs font-bold py-2 px-4 rounded transition"
                            >
                              Save Properties
                            </button>
                            <button
                              type="button"
                              onClick={() => setSelectedItemSlot(null)}
                              className="bg-mcdeploy-bg hover:bg-mcdeploy-border/40 text-mcdeploy-muted hover:text-white border border-mcdeploy-border text-xs py-2 px-4 rounded transition"
                            >
                              Cancel
                            </button>
                          </div>

                          {selectedItemSlot.item && (
                            <div className="flex gap-1.5">
                              <button
                                type="button"
                                onClick={handleRepairItem}
                                className="bg-amber-900/40 hover:bg-amber-900 text-amber-300 hover:text-white border border-amber-800 text-[10px] font-bold py-1.5 px-3 rounded transition"
                              >
                                Repair Durability
                              </button>
                              <button
                                type="button"
                                onClick={handleDuplicateItem}
                                className="bg-blue-900/40 hover:bg-blue-900 text-blue-300 hover:text-white border border-blue-800 text-[10px] font-bold py-1.5 px-3 rounded transition"
                              >
                                Duplicate
                              </button>
                              <button
                                type="button"
                                onClick={handleDeleteItem}
                                className="bg-red-950/60 hover:bg-red-950 border border-red-900 text-red-400 hover:text-white text-[10px] font-bold py-1.5 px-3 rounded transition"
                              >
                                Delete Item
                              </button>
                            </div>
                          )}
                        </div>

                      </div>
                    </div>
                  )}

                </div>
              )}

              {/* SUB TAB: ANALYTICS */}
              {serverTab === 'analytics' && (
                <div className="space-y-4">
                  {/* Header + range picker */}
                  <div className="bg-mcdeploy-card border border-mcdeploy-border p-4 rounded-lg flex items-center justify-between flex-wrap gap-3">
                    <div>
                      <h4 className="text-sm font-bold uppercase tracking-wider text-white flex items-center gap-2">
                        <BarChart3 className="w-4 h-4 text-mcdeploy-green" /> Player Analytics
                      </h4>
                      <p className="text-xs text-mcdeploy-muted mt-1">
                        Sessions, retention, chat and death events for this server.
                      </p>
                    </div>
                    <div className="flex items-center gap-2">
                      {[1, 7, 30, 90].map(d => (
                        <button
                          key={d}
                          onClick={() => setAnalyticsDays(d)}
                          className={`px-3 py-1.5 rounded text-xs font-semibold transition ${
                            analyticsDays === d
                              ? 'bg-mcdeploy-green text-white'
                              : 'bg-mcdeploy-bg border border-mcdeploy-border text-mcdeploy-muted hover:text-white'
                          }`}
                        >
                          {d === 1 ? '24h' : `${d}d`}
                        </button>
                      ))}
                      <button
                        onClick={fetchAnalytics}
                        className="bg-mcdeploy-bg border border-mcdeploy-border text-mcdeploy-muted hover:text-white p-2 rounded transition"
                        title="Refresh"
                      >
                        <RefreshCw className={`w-4 h-4 ${analyticsLoading ? 'animate-spin' : ''}`} />
                      </button>
                    </div>
                  </div>

                  {/* Summary cards */}
                  <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                    {[
                      { label: 'Online now',         value: analyticsSummary?.online_now ?? 0, icon: Power },
                      { label: 'Unique players',    value: analyticsSummary?.unique_players ?? 0, icon: Users },
                      { label: 'Total playtime',    value: formatDuration(analyticsSummary?.total_playtime_seconds ?? 0), icon: Clock },
                      { label: 'Peak concurrent',   value: analyticsSummary?.peak_concurrent ?? 0, icon: TrendingUp },
                    ].map(card => {
                      const Icon = card.icon;
                      return (
                        <div key={card.label} className="bg-mcdeploy-card border border-mcdeploy-border p-4 rounded-lg">
                          <div className="flex items-center gap-2 text-mcdeploy-muted text-xs uppercase tracking-wider">
                            <Icon className="w-3.5 h-3.5" />
                            {card.label}
                          </div>
                          <div className="text-2xl font-bold text-white mt-1">{card.value}</div>
                        </div>
                      );
                    })}
                  </div>

                  {/* Secondary counters */}
                  <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                    {[
                      { label: 'Sessions',   value: analyticsSummary?.total_sessions ?? 0, icon: Activity },
                      { label: 'Avg session', value: formatDuration(analyticsSummary?.avg_session_seconds ?? 0), icon: Clock },
                      { label: 'Chat msgs',  value: analyticsSummary?.chat_count ?? 0, icon: MessageSquare },
                      { label: 'Deaths',     value: analyticsSummary?.death_count ?? 0, icon: Skull },
                    ].map(card => {
                      const Icon = card.icon;
                      return (
                        <div key={card.label} className="bg-mcdeploy-card border border-mcdeploy-border p-4 rounded-lg">
                          <div className="flex items-center gap-2 text-mcdeploy-muted text-xs uppercase tracking-wider">
                            <Icon className="w-3.5 h-3.5" />
                            {card.label}
                          </div>
                          <div className="text-xl font-bold text-white mt-1">{card.value}</div>
                        </div>
                      );
                    })}
                  </div>

                  {/* Hourly bar chart */}
                  <div className="bg-mcdeploy-card border border-mcdeploy-border p-4 rounded-lg">
                    <h5 className="text-sm font-semibold text-white mb-3">Activity by hour of day</h5>
                    <div className="h-56">
                      <Bar
                        data={{
                          labels: analyticsHourly.map(h => `${String(h.hour).padStart(2, '0')}:00`),
                          datasets: [{
                            label: 'Session starts',
                            data: analyticsHourly.map(h => h.sessions),
                            backgroundColor: 'rgba(30, 189, 86, 0.6)',
                            borderColor: '#1ebd56',
                            borderWidth: 1,
                            borderRadius: 3,
                          }],
                        }}
                        options={{
                          maintainAspectRatio: false,
                          plugins: { legend: { display: false } },
                          scales: {
                            x: { ticks: { color: '#8e9e8e' }, grid: { color: 'rgba(255,255,255,0.04)' } },
                            y: { ticks: { color: '#8e9e8e', precision: 0 }, grid: { color: 'rgba(255,255,255,0.04)' } },
                          },
                        }}
                      />
                    </div>
                  </div>

                  {/* Daily line chart */}
                  <div className="bg-mcdeploy-card border border-mcdeploy-border p-4 rounded-lg">
                    <h5 className="text-sm font-semibold text-white mb-3">Daily activity</h5>
                    <div className="h-56">
                      <Line
                        data={{
                          labels: analyticsDaily.map(d => d.day),
                          datasets: [
                            {
                              label: 'Unique players',
                              data: analyticsDaily.map(d => d.unique_players),
                              borderColor: '#5cff96',
                              backgroundColor: 'rgba(92,255,150,0.15)',
                              tension: 0.3,
                              fill: true,
                              yAxisID: 'y',
                            },
                            {
                              label: 'Sessions',
                              data: analyticsDaily.map(d => d.sessions),
                              borderColor: '#1ebd56',
                              backgroundColor: 'rgba(30,189,86,0.05)',
                              tension: 0.3,
                              fill: false,
                              yAxisID: 'y',
                              borderDash: [4, 4],
                            },
                          ],
                        }}
                        options={{
                          maintainAspectRatio: false,
                          plugins: {
                            legend: { labels: { color: '#e2e8f0' } },
                          },
                          scales: {
                            x: { ticks: { color: '#8e9e8e' }, grid: { color: 'rgba(255,255,255,0.04)' } },
                            y: { ticks: { color: '#8e9e8e', precision: 0 }, grid: { color: 'rgba(255,255,255,0.04)' } },
                          },
                        }}
                      />
                    </div>
                  </div>

                  {/* Leaderboard + events side by side */}
                  <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
                    <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden">
                      <div className="p-3 border-b border-mcdeploy-border">
                        <h5 className="text-sm font-semibold text-white">Playtime leaderboard</h5>
                      </div>
                      {analyticsLeaderboard.length === 0 ? (
                        <div className="p-4 text-sm text-mcdeploy-muted">No sessions in this range.</div>
                      ) : (
                        <table className="w-full text-sm">
                          <thead className="text-xs uppercase tracking-wider text-mcdeploy-muted bg-mcdeploy-bg">
                            <tr>
                              <th className="p-2 text-left w-8">#</th>
                              <th className="p-2 text-left">Player</th>
                              <th className="p-2 text-right">Playtime</th>
                              <th className="p-2 text-right">Sessions</th>
                            </tr>
                          </thead>
                          <tbody>
                            {analyticsLeaderboard.map((p, i) => (
                              <tr key={p.username} className="border-t border-mcdeploy-border hover:bg-mcdeploy-bg">
                                <td className="p-2 text-mcdeploy-muted">{i + 1}</td>
                                <td className="p-2 text-white font-medium">{p.username}</td>
                                <td className="p-2 text-right text-mcdeploy-accent">{formatDuration(p.total_seconds)}</td>
                                <td className="p-2 text-right text-mcdeploy-muted">{p.sessions}</td>
                              </tr>
                            ))}
                          </tbody>
                        </table>
                      )}
                    </div>

                    <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden">
                      <div className="p-3 border-b border-mcdeploy-border flex items-center gap-2">
                        <h5 className="text-sm font-semibold text-white flex-1">Event feed</h5>
                        <select
                          value={analyticsEventType}
                          onChange={(e) => setAnalyticsEventType(e.target.value)}
                          className="bg-mcdeploy-bg border border-mcdeploy-border text-white text-xs rounded px-2 py-1"
                        >
                          <option value="">All</option>
                          <option value="chat">Chat</option>
                          <option value="death">Deaths</option>
                          <option value="join">Joins</option>
                          <option value="leave">Leaves</option>
                        </select>
                      </div>
                      <div className="max-h-[420px] overflow-auto">
                        {analyticsEvents.length === 0 ? (
                          <div className="p-4 text-sm text-mcdeploy-muted">No events yet.</div>
                        ) : (
                          <ul className="divide-y divide-mcdeploy-border">
                            {analyticsEvents.map(e => (
                              <li key={e.id} className="p-2 text-xs">
                                <div className="flex items-center gap-2">
                                  <span className="text-mcdeploy-muted font-mono">{e.created_at}</span>
                                  <span className="text-mcdeploy-green font-semibold uppercase text-[10px] tracking-wider">
                                    {e.event_type}
                                  </span>
                                  <span className="text-white font-medium">{e.username}</span>
                                </div>
                                {e.payload && (
                                  <div className="text-mcdeploy-text mt-0.5 ml-1 break-all">{e.payload}</div>
                                )}
                              </li>
                            ))}
                          </ul>
                        )}
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {/* SUB TAB: SCHEDULED TASKS */}
              {serverTab === 'schedule' && (
                <div className="space-y-4">
                  {/* Header */}
                  <div className="bg-mcdeploy-card border border-mcdeploy-border p-4 rounded-lg flex items-center justify-between">
                    <div>
                      <h4 className="text-sm font-bold uppercase tracking-wider text-white flex items-center gap-2">
                        <Calendar className="w-4 h-4 text-mcdeploy-green" /> Scheduled Tasks
                      </h4>
                      <p className="text-xs text-mcdeploy-muted mt-1">
                        Run console commands, restarts, backups, or AI prompts on a schedule.
                      </p>
                    </div>
                    <button
                      onClick={() => setScheduleForm({ ...emptyScheduleForm })}
                      className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold py-2 px-4 rounded text-sm transition flex items-center gap-2"
                    >
                      <Plus className="w-4 h-4" /> New Task
                    </button>
                  </div>

                  {/* Task list */}
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden">
                    {scheduleLoading ? (
                      <div className="p-4 text-sm text-mcdeploy-muted flex items-center gap-2">
                        <Loader className="w-4 h-4 animate-spin" /> Loading tasks…
                      </div>
                    ) : scheduleTasks.length === 0 ? (
                      <div className="p-8 text-center">
                        <Calendar className="w-10 h-10 text-mcdeploy-muted mx-auto mb-3" />
                        <div className="text-white font-medium">No scheduled tasks yet.</div>
                        <div className="text-xs text-mcdeploy-muted mt-1">
                          Create your first task to automate restarts, backups, or console commands.
                        </div>
                      </div>
                    ) : (
                      <table className="w-full text-sm">
                        <thead className="text-xs uppercase tracking-wider text-mcdeploy-muted bg-mcdeploy-bg">
                          <tr>
                            <th className="p-2 text-left">Name</th>
                            <th className="p-2 text-left">Action</th>
                            <th className="p-2 text-left">Schedule</th>
                            <th className="p-2 text-left">Next run</th>
                            <th className="p-2 text-left">Last</th>
                            <th className="p-2 text-right w-56">Controls</th>
                          </tr>
                        </thead>
                        <tbody>
                          {scheduleTasks.map(t => (
                            <React.Fragment key={t.id}>
                              <tr className="border-t border-mcdeploy-border hover:bg-mcdeploy-bg">
                                <td className="p-2">
                                  <div className="flex items-center gap-2">
                                    <span
                                      className={`w-2 h-2 rounded-full ${
                                        t.enabled ? 'bg-mcdeploy-green' : 'bg-mcdeploy-muted'
                                      }`}
                                      title={t.enabled ? 'enabled' : 'disabled'}
                                    />
                                    <span className="text-white font-medium">{t.name}</span>
                                  </div>
                                  {t.payload && (
                                    <div className="text-xs text-mcdeploy-muted ml-4 mt-0.5 font-mono truncate max-w-md">
                                      {t.payload}
                                    </div>
                                  )}
                                </td>
                                <td className="p-2 text-mcdeploy-accent uppercase text-xs tracking-wider">
                                  {t.action_type}
                                </td>
                                <td className="p-2 text-mcdeploy-text text-xs">
                                  <span className="text-mcdeploy-muted uppercase">{t.schedule_kind}</span>{' '}
                                  <span className="font-mono">{t.schedule_value}</span>
                                </td>
                                <td className="p-2 text-mcdeploy-muted text-xs font-mono">{t.next_run_at || '—'}</td>
                                <td className="p-2 text-xs">
                                  {t.last_status ? (
                                    <span
                                      className={`px-1.5 py-0.5 rounded text-[10px] font-semibold uppercase ${
                                        t.last_status === 'success'
                                          ? 'bg-mcdeploy-green/20 text-mcdeploy-green'
                                          : t.last_status === 'skipped'
                                          ? 'bg-yellow-500/20 text-yellow-400'
                                          : 'bg-red-500/20 text-red-400'
                                      }`}
                                    >
                                      {t.last_status}
                                    </span>
                                  ) : (
                                    <span className="text-mcdeploy-muted">—</span>
                                  )}
                                </td>
                                <td className="p-2 text-right">
                                  <div className="inline-flex items-center gap-1">
                                    <button
                                      onClick={() => handleRunScheduleTaskNow(t)}
                                      className="p-1.5 rounded hover:bg-mcdeploy-green/20 text-mcdeploy-green"
                                      title="Run now"
                                    >
                                      <Zap className="w-4 h-4" />
                                    </button>
                                    <button
                                      onClick={() => handleToggleScheduleTask(t)}
                                      className="p-1.5 rounded hover:bg-mcdeploy-bg text-mcdeploy-muted hover:text-white"
                                      title={t.enabled ? 'Disable' : 'Enable'}
                                    >
                                      <Power className="w-4 h-4" />
                                    </button>
                                    <button
                                      onClick={() => setScheduleForm({
                                        id: t.id,
                                        name: t.name,
                                        action_type: t.action_type,
                                        payload: t.payload || '',
                                        schedule_kind: t.schedule_kind,
                                        schedule_value: t.schedule_value,
                                        enabled: t.enabled,
                                      })}
                                      className="p-1.5 rounded hover:bg-mcdeploy-bg text-mcdeploy-muted hover:text-white"
                                      title="Edit"
                                    >
                                      <Settings className="w-4 h-4" />
                                    </button>
                                    <button
                                      onClick={() => {
                                        if (scheduleRunsFor === t.id) { setScheduleRunsFor(null); setScheduleRuns([]); }
                                        else { setScheduleRunsFor(t.id); fetchScheduleRuns(t.id); }
                                      }}
                                      className="p-1.5 rounded hover:bg-mcdeploy-bg text-mcdeploy-muted hover:text-white"
                                      title="History"
                                    >
                                      <Clock className="w-4 h-4" />
                                    </button>
                                    <button
                                      onClick={() => handleDeleteScheduleTask(t)}
                                      className="p-1.5 rounded hover:bg-red-500/20 text-red-400"
                                      title="Delete"
                                    >
                                      <Trash2 className="w-4 h-4" />
                                    </button>
                                  </div>
                                </td>
                              </tr>
                              {scheduleRunsFor === t.id && (
                                <tr className="bg-mcdeploy-bg border-t border-mcdeploy-border">
                                  <td colSpan={6} className="p-3">
                                    <div className="text-xs uppercase tracking-wider text-mcdeploy-muted mb-2">
                                      Recent runs
                                    </div>
                                    {scheduleRuns.length === 0 ? (
                                      <div className="text-xs text-mcdeploy-muted">No runs yet.</div>
                                    ) : (
                                      <ul className="space-y-1 max-h-64 overflow-auto">
                                        {scheduleRuns.map(r => (
                                          <li key={r.id} className="text-xs font-mono flex items-start gap-2">
                                            <span className="text-mcdeploy-muted whitespace-nowrap">
                                              {r.started_at || r.finished_at || '—'}
                                            </span>
                                            <span
                                              className={`whitespace-nowrap font-semibold uppercase ${
                                                r.status === 'success' ? 'text-mcdeploy-green'
                                                : r.status === 'skipped' ? 'text-yellow-400'
                                                : 'text-red-400'
                                              }`}
                                            >
                                              {r.status}
                                            </span>
                                            <span className="text-mcdeploy-text break-all">{r.output}</span>
                                          </li>
                                        ))}
                                      </ul>
                                    )}
                                  </td>
                                </tr>
                              )}
                            </React.Fragment>
                          ))}
                        </tbody>
                      </table>
                    )}
                  </div>

                  {/* Create / Edit modal */}
                  {scheduleForm && (
                    <div className="fixed inset-0 bg-black/70 flex items-center justify-center z-50 p-4">
                      <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg w-full max-w-lg">
                        <div className="p-4 border-b border-mcdeploy-border flex items-center justify-between">
                          <h4 className="text-white font-semibold">
                            {scheduleForm.id ? 'Edit scheduled task' : 'New scheduled task'}
                          </h4>
                          <button
                            onClick={() => setScheduleForm(null)}
                            className="text-mcdeploy-muted hover:text-white"
                          >
                            ✕
                          </button>
                        </div>
                        <div className="p-4 space-y-3">
                          <div>
                            <label className="text-xs uppercase text-mcdeploy-muted tracking-wider">Name</label>
                            <input
                              type="text"
                              value={scheduleForm.name}
                              onChange={(e) => setScheduleForm({ ...scheduleForm, name: e.target.value })}
                              placeholder="e.g. Nightly restart"
                              className="w-full mt-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-sm"
                            />
                          </div>
                          <div className="grid grid-cols-2 gap-3">
                            <div>
                              <label className="text-xs uppercase text-mcdeploy-muted tracking-wider">Action</label>
                              <select
                                value={scheduleForm.action_type}
                                onChange={(e) => setScheduleForm({ ...scheduleForm, action_type: e.target.value })}
                                className="w-full mt-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-sm"
                              >
                                <option value="console">Console command</option>
                                <option value="restart">Restart server</option>
                                <option value="start">Start server</option>
                                <option value="stop">Stop server</option>
                                <option value="backup">Create backup</option>
                                <option value="ai_prompt">AI prompt</option>
                              </select>
                            </div>
                            <div>
                              <label className="text-xs uppercase text-mcdeploy-muted tracking-wider">Schedule</label>
                              <select
                                value={scheduleForm.schedule_kind}
                                onChange={(e) => {
                                  const kind = e.target.value;
                                  const defaults = {
                                    interval: '3600',
                                    daily:    '04:00',
                                    cron:     '0 4 * * *',
                                  };
                                  setScheduleForm({
                                    ...scheduleForm,
                                    schedule_kind: kind,
                                    schedule_value: defaults[kind] || '',
                                  });
                                }}
                                className="w-full mt-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-sm"
                              >
                                <option value="interval">Interval (seconds)</option>
                                <option value="daily">Daily at HH:MM</option>
                                <option value="cron">Cron (5-field)</option>
                              </select>
                            </div>
                          </div>

                          {(scheduleForm.action_type === 'console' || scheduleForm.action_type === 'ai_prompt') && (
                            <div>
                              <label className="text-xs uppercase text-mcdeploy-muted tracking-wider">
                                {scheduleForm.action_type === 'console' ? 'Console command' : 'AI prompt'}
                              </label>
                              <textarea
                                value={scheduleForm.payload}
                                onChange={(e) => setScheduleForm({ ...scheduleForm, payload: e.target.value })}
                                placeholder={
                                  scheduleForm.action_type === 'console'
                                    ? 'say Restarting in 5 minutes'
                                    : 'Check logs for errors and summarize.'
                                }
                                rows={2}
                                className="w-full mt-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-sm font-mono"
                              />
                            </div>
                          )}

                          <div>
                            <label className="text-xs uppercase text-mcdeploy-muted tracking-wider">
                              {scheduleForm.schedule_kind === 'interval' && 'Seconds between runs'}
                              {scheduleForm.schedule_kind === 'daily'    && 'Time of day (HH:MM, local)'}
                              {scheduleForm.schedule_kind === 'cron'     && 'Cron expression (min hour dom mon dow)'}
                            </label>
                            <input
                              type="text"
                              value={scheduleForm.schedule_value}
                              onChange={(e) => setScheduleForm({ ...scheduleForm, schedule_value: e.target.value })}
                              className="w-full mt-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-sm font-mono"
                            />
                            <p className="text-xs text-mcdeploy-muted mt-1">
                              {scheduleForm.schedule_kind === 'interval' && 'Minimum 30 seconds.'}
                              {scheduleForm.schedule_kind === 'daily'    && 'e.g. 04:00 for 4am, 23:30 for 11:30pm.'}
                              {scheduleForm.schedule_kind === 'cron'     && 'e.g. "0 4 * * *" = 4am daily, "*/15 * * * *" = every 15 min.'}
                            </p>
                          </div>

                          <label className="flex items-center gap-2 text-sm text-mcdeploy-text">
                            <input
                              type="checkbox"
                              checked={scheduleForm.enabled}
                              onChange={(e) => setScheduleForm({ ...scheduleForm, enabled: e.target.checked })}
                              className="accent-mcdeploy-green"
                            />
                            Enabled
                          </label>
                        </div>
                        <div className="p-4 border-t border-mcdeploy-border flex justify-end gap-2">
                          <button
                            onClick={() => setScheduleForm(null)}
                            className="px-4 py-2 rounded text-sm text-mcdeploy-muted hover:text-white"
                          >
                            Cancel
                          </button>
                          <button
                            onClick={handleSaveScheduleTask}
                            className="px-4 py-2 rounded bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white text-sm font-semibold"
                          >
                            {scheduleForm.id ? 'Save changes' : 'Create task'}
                          </button>
                        </div>
                      </div>
                    </div>
                  )}
                </div>
              )}

              {/* SUB TAB: BACKUPS */}
              {serverTab === 'backups' && (
                <div className="space-y-4">
                  <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg flex items-center justify-between">
                    <div>
                      <h4 className="text-sm font-bold uppercase tracking-wider text-white">Manual Backup Creator</h4>
                      <p className="text-xs text-mcdeploy-muted mt-1">Zips up game worlds, configurations, and core server records.</p>
                    </div>
                    <button onClick={handleCreateBackup} className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold py-2 px-4 rounded text-sm transition">
                      Create Backup Now
                    </button>
                  </div>

                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden">
                    <div className="overflow-x-auto">
                      <table className="w-full text-left border-collapse">
                        <thead>
                          <tr className="bg-mcdeploy-bg/60 text-xs font-bold text-mcdeploy-muted border-b border-mcdeploy-border">
                            <th className="p-4">Backup Name</th>
                            <th className="p-4">Size (Bytes)</th>
                            <th className="p-4">Created At</th>
                          </tr>
                        </thead>
                        <tbody className="divide-y divide-mcdeploy-border/60 font-mono text-xs">
                          {backupsList.map(b => (
                            <tr key={b.backup_uuid} className="hover:bg-mcdeploy-border/5">
                              <td className="p-4 text-white flex items-center gap-2"><Database className="w-4 h-4 text-mcdeploy-green" /> {b.file_name}</td>
                              <td className="p-4">{b.file_size}</td>
                              <td className="p-4 text-mcdeploy-muted">{b.created_at}</td>
                            </tr>
                          ))}
                          {backupsList.length === 0 && (
                            <tr>
                              <td colSpan="3" className="p-8 text-center text-mcdeploy-muted italic text-sm">No backup archives found. Click create to zip the server state.</td>
                            </tr>
                          )}
                        </tbody>
                      </table>
                    </div>
                  </div>
                </div>
              )}

              {/* SUB TAB: PERFORMANCE METRICS */}
              {serverTab === 'metrics' && (
                <div className="space-y-6">
                  <div className="grid grid-cols-3 gap-6">
                    <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg">
                      <span className="text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Ticks Per Second (TPS)</span>
                      <h4 className="text-3xl font-extrabold text-white mt-1 font-mono">
                        {serverMetrics?.tps ? serverMetrics.tps.toFixed(2) : '0.00'}
                      </h4>
                      <div className="text-xs text-mcdeploy-muted mt-2">Target value: 20.0 (Optimal)</div>
                    </div>
                    
                    <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg">
                      <span className="text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Average Tick Time (MSPT)</span>
                      <h4 className="text-3xl font-extrabold text-white mt-1 font-mono">
                        {serverMetrics?.mspt ? serverMetrics.mspt.toFixed(1) : '0.0'} ms
                      </h4>
                      <div className="text-xs text-mcdeploy-muted mt-2">Tick limit: 50.0 ms (Lag boundary)</div>
                    </div>

                    <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg">
                      <span className="text-xs font-bold uppercase tracking-wider text-mcdeploy-muted">Players Online</span>
                      <h4 className="text-3xl font-extrabold text-white mt-1 font-mono flex items-center gap-1.5">
                        <Users className="w-7 h-7 text-mcdeploy-green" />
                        {serverMetrics?.players_online || 0} / {serverMetrics?.players_max || 0}
                      </h4>
                      <div className="text-xs text-mcdeploy-muted mt-2">Active player sessions on server</div>
                    </div>
                  </div>

                  <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg">
                    <h4 className="text-sm font-bold uppercase tracking-wider text-mcdeploy-muted mb-4">Minecraft Server Performance Graph</h4>
                    <div className="h-56">
                      <span className="text-xs font-semibold text-white">Live Server Ticks (TPS) History</span>
                      <Line data={tpsChartData} options={chartOptions} />
                    </div>
                  </div>

                  <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg space-y-6">
                    <div>
                      <h4 className="text-sm font-bold uppercase tracking-wider text-white flex items-center gap-2 font-mono">
                        <Cpu className="w-5 h-5 text-mcdeploy-green" /> C++ Native Process Scheduling & Optimization
                      </h4>
                      <p className="text-xs text-mcdeploy-muted mt-1 font-sans">
                        Control how the system schedules CPU cores and memory allocations for your server's JVM process in real-time.
                      </p>
                    </div>

                    <div className="grid grid-cols-1 md:grid-cols-2 gap-6 font-sans">
                      <div className="bg-mcdeploy-bg/40 p-4 border border-mcdeploy-border/60 rounded-lg flex flex-col justify-between gap-4">
                        <div className="flex items-start justify-between gap-4">
                          <div>
                            <span className="text-xs font-bold text-white block">Smart Optimization Watchdog</span>
                            <span className="text-[11px] text-mcdeploy-muted mt-0.5 block">
                              Automatically scales process priority based on server state: High during startup, Above Normal when players are online, Normal when idle.
                            </span>
                          </div>
                          <button
                            type="button"
                            onClick={() => handleUpdatePerformance(!perfSmartOpt, perfPriority)}
                            className={`relative inline-flex items-center h-6 w-11 rounded-full transition-colors duration-200 focus:outline-none flex-shrink-0 ${
                              perfSmartOpt ? 'bg-mcdeploy-green' : 'bg-mcdeploy-border'
                            }`}
                          >
                            <span
                              className={`inline-block w-4 h-4 transform rounded-full bg-white transition-transform duration-200 ${
                                perfSmartOpt ? 'translate-x-6' : 'translate-x-1'
                              }`}
                            />
                          </button>
                        </div>
                        <div className="text-[10px] text-mcdeploy-muted border-t border-mcdeploy-border/40 pt-2 flex items-center gap-1.5 font-sans">
                          <span className="w-1.5 h-1.5 rounded-full bg-mcdeploy-green animate-pulse" />
                          Recommended for shared host systems to conserve resources.
                        </div>
                      </div>

                      <div className="bg-mcdeploy-bg/40 p-4 border border-mcdeploy-border/60 rounded-lg flex flex-col justify-between gap-4">
                        <div className="space-y-2">
                          <label className="block text-xs font-bold text-white">Manual Process Priority Class</label>
                          <span className="text-[11px] text-mcdeploy-muted block">
                            Directly adjust the priority class of the Java process on the host operating system.
                          </span>
                          <select
                            disabled={perfSmartOpt || perfLoading}
                            value={perfPriority}
                            onChange={(e) => handleUpdatePerformance(perfSmartOpt, e.target.value)}
                            className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white text-xs px-3 py-2 rounded focus:outline-none focus:border-mcdeploy-green disabled:opacity-50 transition"
                          >
                            <option value="idle">Idle (Lowest priority, saves host CPU)</option>
                            <option value="below_normal">Below Normal (Conserves host CPU)</option>
                            <option value="normal">Normal (Default scheduler priority)</option>
                            <option value="above_normal">Above Normal (Increases responsiveness)</option>
                            <option value="high">High (Maximum priority, minimizes chunk lag)</option>
                          </select>
                        </div>
                        <div className="text-[10px] text-mcdeploy-muted border-t border-mcdeploy-border/40 pt-2 flex items-center justify-between">
                          <span>Current Priority Status:</span>
                          <span className="font-mono font-bold text-white uppercase bg-mcdeploy-border px-2 py-0.5 rounded text-[9px]">
                            {perfSmartOpt ? 'Managed (Smart)' : perfPriority}
                          </span>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              )}

              {/* SUB TAB: SERVER HEALTH SCORE */}
              {serverTab === 'health' && (
                <div className="grid grid-cols-1 lg:grid-cols-[280px_1fr] gap-6">
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6 flex flex-col items-center text-center gap-4">
                    <div
                      className="w-40 h-40 rounded-full flex flex-col items-center justify-center"
                      style={{ background: `radial-gradient(circle, #141814 58%, transparent 60%), conic-gradient(#1ebd56 ${(healthData?.score || 0) * 3.6}deg, #242b24 0deg)` }}
                    >
                      <strong className="text-5xl text-white font-black">{healthLoading ? '…' : healthData?.score ?? '--'}</strong>
                      <span className="text-xs text-mcdeploy-muted">out of 100</span>
                    </div>
                    <div>
                      <h4 className="text-lg font-bold text-white">{healthData?.grade || 'Calculating health'}</h4>
                      <p className="text-xs text-mcdeploy-muted mt-1">Live deterministic scoring across availability, resources, storage, logs, and backups.</p>
                    </div>
                    <button onClick={fetchHealthScore} disabled={healthLoading} className="bg-mcdeploy-border hover:bg-mcdeploy-green text-white text-xs font-bold px-4 py-2 rounded flex items-center gap-2">
                      <RefreshCw className={`w-4 h-4 ${healthLoading ? 'animate-spin' : ''}`} /> Refresh score
                    </button>
                  </div>
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6 space-y-6">
                    <div>
                      <h4 className="text-sm font-bold uppercase tracking-wider text-white">Health breakdown</h4>
                      <p className="text-xs text-mcdeploy-muted mt-1">Generated {healthData?.generated_at || 'when this tab opens'}</p>
                    </div>
                    <div className="space-y-4">
                      {(healthData?.components || []).map(component => {
                        const percent = component.maximum ? Math.round(component.score / component.maximum * 100) : 0;
                        const color = component.status === 'critical' ? 'bg-red-500' : component.status === 'warning' ? 'bg-yellow-500' : 'bg-mcdeploy-green';
                        return (
                          <div key={component.key}>
                            <div className="flex items-center justify-between text-xs mb-1.5">
                              <span className="text-white font-bold capitalize">{component.key}</span>
                              <span className="font-mono text-mcdeploy-muted">{component.score}/{component.maximum}</span>
                            </div>
                            <div className="h-2 rounded-full bg-mcdeploy-bg overflow-hidden"><div className={`h-full ${color}`} style={{ width: `${percent}%` }} /></div>
                            <p className="text-[11px] text-mcdeploy-muted mt-1">{component.evidence}</p>
                          </div>
                        );
                      })}
                    </div>
                    <div className="border-t border-mcdeploy-border pt-4">
                      <h5 className="text-xs font-bold text-white mb-2">Recommended actions</h5>
                      <ul className="list-disc pl-5 space-y-1.5 text-xs text-mcdeploy-muted">
                        {(healthData?.recommendations?.length ? healthData.recommendations : ['No immediate action is recommended.']).map((item, index) => <li key={index}>{item}</li>)}
                      </ul>
                    </div>
                  </div>
                </div>
              )}

              {/* SUB TAB: AI AUTOMATION RULES */}
              {serverTab === 'automation' && (
                <div className="grid grid-cols-1 xl:grid-cols-[380px_1fr] gap-6">
                  <form onSubmit={createAutomationRule} className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6 space-y-4">
                    <div>
                      <h4 className="text-sm font-bold uppercase tracking-wider text-white flex items-center gap-2"><Zap className="w-4 h-4 text-mcdeploy-green" /> Create rule</h4>
                      <p className="text-xs text-mcdeploy-muted mt-1">Monitor server conditions and execute lifecycle, console, backup, or AI actions automatically.</p>
                    </div>
                    <label className="block text-xs font-bold text-white">Rule name
                      <input value={automationForm.name} onChange={(e) => setAutomationForm({ ...automationForm, name:e.target.value })} className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border px-3 py-2 rounded text-sm text-white focus:outline-none focus:border-mcdeploy-green" />
                    </label>
                    <div className="grid grid-cols-2 gap-3">
                      <label className="block text-xs font-bold text-white">Trigger
                        <select value={automationForm.trigger_type} onChange={(e) => setAutomationForm({ ...automationForm, trigger_type:e.target.value })} className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border px-3 py-2 rounded text-xs text-white">
                          <option value="server_offline">Server offline</option><option value="cpu_above">CPU above %</option><option value="ram_above">RAM above %</option><option value="disk_below">Disk below GB</option><option value="log_contains">Log contains</option>
                        </select>
                      </label>
                      <label className="block text-xs font-bold text-white">Threshold
                        <input type="number" step="0.1" value={automationForm.threshold} onChange={(e) => setAutomationForm({ ...automationForm, threshold:Number(e.target.value) })} className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border px-3 py-2 rounded text-xs text-white" />
                      </label>
                    </div>
                    <label className="block text-xs font-bold text-white">Log match text
                      <input value={automationForm.condition_value} onChange={(e) => setAutomationForm({ ...automationForm, condition_value:e.target.value })} placeholder="OutOfMemoryError" className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border px-3 py-2 rounded text-xs text-white" />
                    </label>
                    <div className="grid grid-cols-2 gap-3">
                      <label className="block text-xs font-bold text-white">Action
                        <select value={automationForm.action_type} onChange={(e) => setAutomationForm({ ...automationForm, action_type:e.target.value })} className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border px-3 py-2 rounded text-xs text-white">
                          <option value="start">Start</option><option value="restart">Restart</option><option value="backup">Backup</option><option value="console">Console command</option><option value="ai_prompt">AI investigation</option><option value="stop">Stop</option>
                        </select>
                      </label>
                      <label className="block text-xs font-bold text-white">Cooldown seconds
                        <input type="number" min="30" value={automationForm.cooldown_seconds} onChange={(e) => setAutomationForm({ ...automationForm, cooldown_seconds:Number(e.target.value) })} className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border px-3 py-2 rounded text-xs text-white" />
                      </label>
                    </div>
                    <label className="block text-xs font-bold text-white">Action payload
                      <input value={automationForm.action_payload} onChange={(e) => setAutomationForm({ ...automationForm, action_payload:e.target.value })} placeholder="Required for console or AI actions" className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border px-3 py-2 rounded text-xs text-white" />
                    </label>
                    <button type="submit" className="w-full bg-mcdeploy-green hover:bg-mcdeploy-darkgreen text-white font-bold py-2.5 rounded text-sm">Create automation rule</button>
                  </form>
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6">
                    <div className="flex items-center justify-between mb-4">
                      <div><h4 className="text-sm font-bold uppercase tracking-wider text-white">Rules</h4><p className="text-xs text-mcdeploy-muted mt-1">Evaluated every ten seconds with per-rule cooldowns.</p></div>
                      <button onClick={fetchAutomationRules} className="text-mcdeploy-muted hover:text-white"><RefreshCw className={`w-4 h-4 ${automationLoading ? 'animate-spin' : ''}`} /></button>
                    </div>
                    <div className="space-y-3">
                      {automationRules.map(rule => (
                        <div key={rule.id} className={`border border-mcdeploy-border rounded-lg p-4 bg-mcdeploy-bg/40 ${rule.enabled ? '' : 'opacity-50'}`}>
                          <div className="flex items-start justify-between gap-4"><div><strong className="text-sm text-white">{rule.name}</strong><p className="text-xs text-mcdeploy-muted mt-1">{rule.trigger_type.replaceAll('_',' ')} → {rule.action_type.replaceAll('_',' ')} · cooldown {rule.cooldown_seconds}s</p></div><span className={`text-[10px] font-bold px-2 py-1 rounded ${rule.enabled ? 'bg-green-950 text-green-400' : 'bg-slate-800 text-slate-400'}`}>{rule.enabled ? 'ON' : 'OFF'}</span></div>
                          {rule.last_output && <p className="text-[11px] text-mcdeploy-muted mt-3 border-t border-mcdeploy-border pt-2">Last: {rule.last_status} — {rule.last_output}</p>}
                          <div className="flex gap-2 mt-3"><button onClick={() => runAutomationRule(rule)} className="text-xs bg-mcdeploy-green text-white px-3 py-1.5 rounded">Run now</button><button onClick={() => toggleAutomationRule(rule)} className="text-xs bg-mcdeploy-border text-white px-3 py-1.5 rounded">{rule.enabled ? 'Disable' : 'Enable'}</button><button onClick={() => deleteAutomationRule(rule)} className="text-xs text-red-400 px-2 py-1.5">Delete</button></div>
                        </div>
                      ))}
                      {!automationRules.length && <div className="text-center text-sm text-mcdeploy-muted py-12">No automation rules yet.</div>}
                    </div>
                  </div>
                </div>
              )}

              {/* SUB TAB: MAINTENANCE MODE */}
              {serverTab === 'maintenance' && (
                <div className="grid grid-cols-1 lg:grid-cols-[1fr_360px] gap-6">
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6 space-y-5">
                    <div className="flex items-start justify-between gap-4"><div><h4 className="text-sm font-bold uppercase tracking-wider text-white">Maintenance mode</h4><p className="text-xs text-mcdeploy-muted mt-1">Back up, notify players, and prevent new non-whitelisted joins.</p></div><span className={`text-xs font-bold px-3 py-1.5 rounded border ${maintenance.enabled ? 'bg-yellow-950/40 text-yellow-400 border-yellow-800' : 'bg-slate-900 text-slate-400 border-mcdeploy-border'}`}>{maintenance.enabled ? 'ENABLED' : 'DISABLED'}</span></div>
                    <label className="block text-xs font-bold text-white">Player announcement
                      <textarea rows="4" value={maintenance.message || ''} onChange={(e) => setMaintenance({ ...maintenance, message:e.target.value })} className="mt-1.5 w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded text-sm focus:outline-none focus:border-mcdeploy-green" />
                    </label>
                    <label className="flex items-center gap-2 text-sm text-white"><input type="checkbox" checked={maintenance.prevent_joins !== false} onChange={(e) => setMaintenance({ ...maintenance, prevent_joins:e.target.checked })} className="accent-mcdeploy-green" /> Prevent new joins with the whitelist</label>
                    <label className="flex items-center gap-2 text-sm text-white"><input type="checkbox" checked={maintenance.backup_on_enable !== false} onChange={(e) => setMaintenance({ ...maintenance, backup_on_enable:e.target.checked })} className="accent-mcdeploy-green" /> Create a safety backup before enabling</label>
                    <div className="flex flex-wrap gap-3 pt-3 border-t border-mcdeploy-border"><button onClick={saveMaintenance} disabled={maintenanceLoading} className="bg-mcdeploy-border hover:bg-mcdeploy-border/70 text-white text-sm font-bold px-4 py-2.5 rounded">Save settings</button><button onClick={() => setMaintenanceMode(true)} disabled={maintenanceLoading || maintenance.enabled} className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen disabled:opacity-40 text-white text-sm font-bold px-4 py-2.5 rounded">Enable maintenance</button><button onClick={() => setMaintenanceMode(false)} disabled={maintenanceLoading || !maintenance.enabled} className="bg-red-950 hover:bg-red-900 disabled:opacity-40 text-red-300 text-sm font-bold px-4 py-2.5 rounded">Disable</button>{maintenanceLoading && <Loader className="w-5 h-5 animate-spin text-mcdeploy-green self-center" />}</div>
                  </div>
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-6">
                    <h4 className="text-sm font-bold text-white">Safe maintenance sequence</h4>
                    <ol className="list-decimal pl-5 mt-4 space-y-3 text-xs text-mcdeploy-muted"><li>Create a safety backup</li><li>Announce maintenance in-game</li><li>Enable and enforce the whitelist</li><li>Preserve the previous whitelist state</li><li>Restore access when maintenance ends</li></ol>
                    {maintenance.enabled_at && <p className="text-[11px] text-yellow-400 mt-5 border-t border-mcdeploy-border pt-3">Enabled {maintenance.enabled_at} by {maintenance.enabled_by || 'admin'}</p>}
                  </div>
                </div>
              )}

              {/* SUB TAB: AI EDITOR */}
              {serverTab === 'ai' && (
                <div className="space-y-4">
                  {/* Header: Mode toggle, undo, clear, usage stats */}
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-4">
                    <div className="flex flex-wrap items-center justify-between gap-3">
                      <div className="flex items-center gap-3">
                        <Bot className="w-6 h-6 text-mcdeploy-green" />
                        <div>
                          <h4 className="text-sm font-bold text-white">AI Server Assistant</h4>
                          <p className="text-xs text-mcdeploy-muted mt-0.5">
                            {aiAgentMode
                              ? 'Agent Mode â€” can inspect, edit files, run console commands (with confirmations)'
                              : 'Read-Only Mode â€” inspect files, logs, metrics; cannot modify anything'}
                          </p>
                        </div>
                      </div>
                      <div className="flex items-center gap-2 flex-wrap">
                        <div className="text-[10px] text-mcdeploy-muted bg-black/40 border border-mcdeploy-border px-2 py-1 rounded font-mono">
                          {aiUsage.tokens_total?.toLocaleString?.() || 0} tokens Â· {aiUsage.request_count || 0} req Â· undo: {aiUsage.undo_stack_size || 0}
                        </div>
                        <button
                          onClick={handleAiUndo}
                          disabled={!aiUsage.undo_stack_size}
                          title="Undo the last AI file change"
                          className="bg-mcdeploy-border hover:bg-mcdeploy-border/80 disabled:opacity-40 disabled:cursor-not-allowed text-white text-xs font-semibold py-2 px-3 rounded transition flex items-center gap-2"
                        >
                          <RotateCcw className="w-3.5 h-3.5" /> Undo
                        </button>
                        <button
                          onClick={handleClearAiChat}
                          disabled={aiMessages.length === 0}
                          className="bg-mcdeploy-border hover:bg-mcdeploy-border/80 disabled:opacity-40 disabled:cursor-not-allowed text-white text-xs font-semibold py-2 px-3 rounded transition flex items-center gap-2"
                        >
                          <Trash2 className="w-3.5 h-3.5" /> Clear
                        </button>
                        <button
                          onClick={() => setAiAgentMode(!aiAgentMode)}
                          title={aiAgentMode ? 'Agent mode: ON' : 'Agent mode: OFF'}
                          className={`relative inline-flex items-center h-8 w-16 rounded-full transition-colors duration-200 focus:outline-none ${aiAgentMode ? 'bg-mcdeploy-green' : 'bg-mcdeploy-border'}`}
                        >
                          <span className={`inline-block w-6 h-6 transform rounded-full bg-white transition-transform duration-200 ${aiAgentMode ? 'translate-x-9' : 'translate-x-1'}`} />
                        </button>
                      </div>
                    </div>
                  </div>

                  {/* Chat messages */}
                  <div className="bg-black/90 border border-mcdeploy-border rounded-lg p-6 h-[500px] overflow-y-auto flex flex-col gap-4">
                    {aiMessages.length === 0 && !aiLoading && (
                      <div className="flex-1 flex items-center justify-center text-center">
                        <div className="space-y-3">
                          <Bot className="w-16 h-16 text-mcdeploy-green mx-auto" />
                          <h3 className="text-lg font-bold text-white">Ask me anything about this server.</h3>
                          <p className="text-sm text-mcdeploy-muted max-w-md">
                            I can read your files, search logs, check metrics, {aiAgentMode ? 'edit configs, and run console commands.' : 'and explain configuration.'}
                          </p>
                          <div className="text-xs text-mcdeploy-muted pt-2">
                            Try <code className="bg-slate-900 px-1 rounded">/logs</code>, <code className="bg-slate-900 px-1 rounded">/plugins</code>, or "Why did the server crash last night?"
                          </div>
                        </div>
                      </div>
                    )}

                    {aiMessages.map((msg, idx) => (
                      <div key={idx} className={`flex gap-3 ${msg.role === 'user' ? 'flex-row-reverse' : ''}`}>
                        <div className={`flex-shrink-0 w-8 h-8 rounded-full flex items-center justify-center ${
                          msg.role === 'user' ? 'bg-blue-600' : msg.role === 'error' ? 'bg-red-600' : 'bg-mcdeploy-green'
                        }`}>
                          {msg.role === 'user'
                            ? <Users className="w-4 h-4 text-white" />
                            : msg.role === 'error'
                              ? <AlertTriangle className="w-4 h-4 text-white" />
                              : <Bot className="w-4 h-4 text-white" />}
                        </div>
                        <div className={`flex-1 max-w-[85%] ${msg.role === 'user' ? 'text-right' : ''}`}>
                          <div className={`inline-block rounded-lg px-4 py-3 ${
                            msg.role === 'user' ? 'bg-blue-600 text-white'
                              : msg.role === 'error' ? 'bg-red-900/40 border border-red-800 text-red-200'
                              : 'bg-mcdeploy-card border border-mcdeploy-border text-white'
                          }`}>
                            {msg.role === 'user'
                              ? <p className="text-sm whitespace-pre-wrap break-words">{msg.content}</p>
                              : <div className="space-y-1 text-left">{renderMarkdown(msg.content)}</div>}
                          </div>

                          {/* Tool step trace */}
                          {aiShowSteps && msg.role === 'assistant' && Array.isArray(msg.steps) && msg.steps.some(s => s.kind === 'tool_call') && (
                            <div className="mt-2 text-left">
                              <details className="bg-slate-950/70 border border-mcdeploy-border/60 rounded p-2 text-xs">
                                <summary className="cursor-pointer text-mcdeploy-muted hover:text-white flex items-center gap-2">
                                  <Code className="w-3 h-3" />
                                  {msg.steps.filter(s => s.kind === 'tool_call').length} tool call(s) Â· click to expand
                                </summary>
                                <div className="mt-2 space-y-2 pl-1">
                                  {msg.steps.map((s, sIdx) => (
                                    s.kind === 'tool_call' ? (
                                      <div key={sIdx} className="border-l-2 border-mcdeploy-green/60 pl-2">
                                        <div className="font-mono text-mcdeploy-green">â†’ {s.tool}({JSON.stringify(s.arguments).slice(0, 120)}{JSON.stringify(s.arguments).length > 120 ? 'â€¦' : ''})</div>
                                      </div>
                                    ) : s.kind === 'tool_result' ? (
                                      <div key={sIdx} className="border-l-2 border-mcdeploy-border pl-2 text-mcdeploy-muted whitespace-pre-wrap font-mono">
                                        {(s.result || '').slice(0, 400)}{(s.result || '').length > 400 ? 'â€¦' : ''}
                                        <div className="text-[10px] mt-1 text-mcdeploy-muted/60">{s.latency_ms}ms{s.needs_confirmation ? ' Â· needs confirmation' : ''}</div>
                                      </div>
                                    ) : null
                                  ))}
                                </div>
                              </details>
                            </div>
                          )}

                          {/* Suggested follow-ups */}
                          {msg.role === 'assistant' && Array.isArray(msg.suggestions) && msg.suggestions.length > 0 && (
                            <div className="mt-2 flex flex-wrap gap-1.5">
                              {msg.suggestions.map((s, sidx) => (
                                <button
                                  key={sidx}
                                  onClick={() => { setAiInput(s); }}
                                  className="text-[11px] bg-mcdeploy-card border border-mcdeploy-border hover:border-mcdeploy-green hover:text-mcdeploy-green text-mcdeploy-muted px-2 py-1 rounded transition"
                                >
                                  {s}
                                </button>
                              ))}
                            </div>
                          )}

                          <div className="text-[10px] text-mcdeploy-muted mt-1 px-1">
                            {msg.timestamp}
                            {msg.tokens ? ` Â· ${(msg.tokens.prompt + msg.tokens.completion).toLocaleString()} tokens` : ''}
                          </div>
                        </div>
                      </div>
                    ))}

                    {aiLoading && (
                      <div className="flex gap-3">
                        <div className="flex-shrink-0 w-8 h-8 rounded-full flex items-center justify-center bg-mcdeploy-green">
                          <Bot className="w-4 h-4 text-white" />
                        </div>
                        <div className="flex-1">
                          <div className="inline-block rounded-lg px-4 py-3 bg-mcdeploy-card border border-mcdeploy-border">
                            <div className="flex items-center gap-2 text-sm text-mcdeploy-muted">
                              <Loader className="w-4 h-4 animate-spin" />
                              Thinking, reading files, checking your serverâ€¦
                            </div>
                          </div>
                        </div>
                      </div>
                    )}

                    <div ref={aiEndRef}></div>
                  </div>

                  {/* Input row with slash-command palette */}
                  <div className="relative">
                    {aiSlashOpen && aiInput.startsWith('/') && (
                      <div className="absolute bottom-full mb-2 left-0 right-0 bg-mcdeploy-card border border-mcdeploy-border rounded-lg shadow-2xl max-h-60 overflow-y-auto z-10">
                        {AI_SLASH_COMMANDS
                          .filter(c => c.cmd.startsWith(aiInput.split(/\s+/)[0].toLowerCase()))
                          .map(c => (
                            <button
                              key={c.cmd}
                              onClick={() => { setAiInput(c.cmd); setAiSlashOpen(false); }}
                              className="w-full text-left px-3 py-2 hover:bg-black/40 border-b border-mcdeploy-border/40 flex items-center justify-between text-xs"
                            >
                              <span className="font-mono text-mcdeploy-green">{c.cmd}</span>
                              <span className="text-mcdeploy-muted">{c.hint}</span>
                            </button>
                          ))}
                      </div>
                    )}
                    <form onSubmit={handleAiChat} className="flex items-center gap-2">
                      <input
                        type="text"
                        value={aiInput}
                        onChange={(e) => { setAiInput(e.target.value); setAiSlashOpen(e.target.value.startsWith('/')); }}
                        onKeyDown={(e) => { if (e.key === 'Escape') setAiSlashOpen(false); }}
                        onFocus={() => { if (aiInput.startsWith('/')) setAiSlashOpen(true); }}
                        disabled={aiLoading}
                        placeholder='Ask about your server, or type "/" for commandsâ€¦'
                        className="flex-1 bg-mcdeploy-card border border-mcdeploy-border px-4 py-3 rounded text-sm text-white focus:outline-none focus:border-mcdeploy-green disabled:opacity-50 disabled:cursor-not-allowed"
                      />
                      <button
                        type="submit"
                        disabled={aiLoading || !aiInput.trim()}
                        className="bg-mcdeploy-green hover:bg-mcdeploy-green/80 disabled:opacity-40 disabled:cursor-not-allowed text-black text-sm font-bold py-3 px-4 rounded transition flex items-center gap-2"
                      >
                        <Send className="w-4 h-4" /> Send
                      </button>
                    </form>
                  </div>
                </div>
              )}

              {/* Confirmation modal for dangerous AI actions */}
              {aiConfirmModal && (
                <div className="fixed inset-0 bg-black/70 z-50 flex items-center justify-center p-4">
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg max-w-2xl w-full p-6 shadow-2xl">
                    <div className="flex items-start gap-3 mb-4">
                      <AlertTriangle className="w-6 h-6 text-yellow-500 flex-shrink-0 mt-0.5" />
                      <div className="flex-1">
                        <h3 className="text-lg font-bold text-white">Confirm AI Action</h3>
                        <p className="text-sm text-mcdeploy-muted mt-1">
                          The assistant wants to run <code className="bg-slate-900 px-1.5 py-0.5 rounded text-mcdeploy-green font-mono text-xs">{aiConfirmModal.tool}</code>.
                          Review the details below before approving.
                        </p>
                      </div>
                    </div>

                    <div className="bg-slate-950 border border-mcdeploy-border/60 rounded p-3 text-xs font-mono text-mcdeploy-muted mb-3 whitespace-pre-wrap max-h-64 overflow-y-auto">
                      {JSON.stringify(aiConfirmModal.arguments, null, 2)}
                    </div>

                    {aiConfirmModal.reason && (
                      <p className="text-xs text-mcdeploy-muted mb-4">{aiConfirmModal.reason}</p>
                    )}

                    <div className="flex items-center justify-end gap-2">
                      <button
                        onClick={() => handleRejectAction(aiConfirmModal)}
                        className="bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-white text-sm font-semibold py-2 px-4 rounded transition"
                      >
                        Reject
                      </button>
                      <button
                        onClick={() => handleApproveAction(aiConfirmModal)}
                        className="bg-mcdeploy-green hover:bg-mcdeploy-green/80 text-black text-sm font-bold py-2 px-4 rounded transition flex items-center gap-2"
                      >
                        <Check className="w-4 h-4" /> Approve & Run
                      </button>
                    </div>
                  </div>
                </div>
              )}

              {/* SUB TAB: ADDON INSTALLER */}
              {serverTab === 'addons' && (
                <div className="space-y-6">
                  {/* CURRENTLY INSTALLED CARD */}
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg overflow-hidden shadow-lg">
                    <div className="p-4 bg-mcdeploy-bg/40 border-b border-mcdeploy-border flex items-center justify-between">
                      <span className="text-sm font-bold text-white flex items-center gap-2">
                        <Puzzle className="w-4 h-4 text-mcdeploy-green" /> 
                        Currently Installed {['paper', 'purpur', 'spigot'].includes(selectedServer.software_type?.toLowerCase()) ? 'Plugins' : 'Mods'}
                      </span>
                      <span className="text-xs bg-mcdeploy-border px-2.5 py-1 rounded-full text-mcdeploy-muted font-bold">
                        {installedAddons.length} {installedAddons.length === 1 ? 'file' : 'files'}
                      </span>
                    </div>
                    <div className="p-4">
                      {installedAddons.length === 0 ? (
                        <div className="p-6 text-center text-sm text-mcdeploy-muted font-medium bg-mcdeploy-bg/10 border border-dashed border-mcdeploy-border/40 rounded-lg">
                          No {['paper', 'purpur', 'spigot'].includes(selectedServer.software_type?.toLowerCase()) ? 'plugins' : 'mods'} currently detected in this server.
                        </div>
                      ) : (
                        <div className="grid grid-cols-1 md:grid-cols-2 gap-3 max-h-60 overflow-y-auto pr-1">
                          {installedAddons.map(addon => (
                            <div key={addon.filename} className="p-3 bg-mcdeploy-bg/30 border border-mcdeploy-border/60 hover:border-mcdeploy-border rounded-lg flex items-center justify-between gap-3 text-sm transition duration-150">
                              <div className="min-w-0 flex-1">
                                <div className="font-bold text-white truncate font-mono text-xs" title={addon.filename}>
                                  {addon.filename}
                                </div>
                                <div className="text-xs text-mcdeploy-muted mt-0.5">
                                  {(addon.size / 1024 / 1024).toFixed(2)} MB
                                </div>
                              </div>
                              <button
                                onClick={() => handleUninstallAddon(addon.filename)}
                                disabled={uninstallingAddon === addon.filename}
                                className="text-red-400 hover:text-red-300 hover:bg-red-950/20 p-1.5 rounded transition duration-150 disabled:opacity-50"
                                title="Delete file"
                              >
                                {uninstallingAddon === addon.filename ? (
                                  <Loader className="w-4 h-4 animate-spin" />
                                ) : (
                                  <Trash2 className="w-4 h-4" />
                                )}
                              </button>
                            </div>
                          ))}
                        </div>
                      )}
                    </div>
                  </div>

                  {/* REPOSITORY SEARCH & VERSION INSTALLER SECTION */}
                  <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
                    {/* SEARCH PANEL */}
                    <div className="lg:col-span-7 bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-5 space-y-5 shadow-lg">
                      <h3 className="text-sm font-bold text-white flex items-center gap-2 border-b border-mcdeploy-border/40 pb-3">
                        <Search className="w-4 h-4 text-mcdeploy-green" /> Search Addon Database
                      </h3>

                      <div className="flex items-center gap-4">
                        <span className="text-xs font-bold text-white uppercase tracking-wider">Source Repository:</span>
                        <div className="flex items-center gap-2">
                          <button
                            type="button"
                            onClick={() => { setAddonSource('modrinth'); setAddonSearchResults([]); setSelectedAddon(null); setSelectedAddonVersion(null); }}
                            className={`px-3.5 py-1.5 rounded text-xs font-bold transition duration-150 ${addonSource === 'modrinth' ? 'bg-mcdeploy-green text-white shadow' : 'bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-mcdeploy-muted hover:text-white'}`}
                          >
                            Modrinth
                          </button>
                          <button
                            type="button"
                            onClick={() => { setAddonSource('curseforge'); setAddonSearchResults([]); setSelectedAddon(null); setSelectedAddonVersion(null); }}
                            className={`px-3.5 py-1.5 rounded text-xs font-bold transition duration-150 ${addonSource === 'curseforge' ? 'bg-mcdeploy-green text-white shadow' : 'bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-mcdeploy-muted hover:text-white'}`}
                          >
                            CurseForge
                          </button>
                        </div>
                      </div>

                      {addonSource === 'curseforge' && (
                        <div className="space-y-1.5">
                          <label className="block text-xs font-bold text-mcdeploy-muted uppercase tracking-wide">CurseForge API Key (Optional)</label>
                          <input
                            type="password"
                            value={curseforgeApiKey}
                            onChange={(e) => setCurseforgeApiKey(e.target.value)}
                            placeholder="Leave blank to use default public developer key"
                            className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2.5 rounded text-xs focus:outline-none focus:border-mcdeploy-green font-mono"
                          />
                        </div>
                      )}

                      <div className="flex gap-2">
                        <input
                          type="text"
                          value={addonQuery}
                          onChange={(e) => setAddonQuery(e.target.value)}
                          onKeyDown={(e) => { if (e.key === 'Enter') { e.preventDefault(); handleSearchAddons(); } }}
                          placeholder={`Search for ${['paper', 'purpur', 'spigot'].includes(selectedServer.software_type?.toLowerCase()) ? 'plugins (e.g. EssentialsX, Geyser, ViaVersion)' : 'mods (e.g. Sodium, Lithium, Waystones)'}...`}
                          className="flex-1 bg-mcdeploy-bg border border-mcdeploy-border text-white px-4 py-2.5 rounded text-sm focus:outline-none focus:border-mcdeploy-green"
                        />
                        <button
                          type="button"
                          onClick={handleSearchAddons}
                          disabled={searchingAddons || !addonQuery.trim()}
                          className="bg-mcdeploy-green hover:bg-mcdeploy-darkgreen disabled:opacity-50 disabled:cursor-not-allowed text-white px-5 py-2.5 rounded text-sm font-bold transition duration-150 flex items-center gap-1.5 shadow-md shadow-mcdeploy-green/10"
                        >
                          {searchingAddons ? (
                            <Loader className="w-4 h-4 animate-spin text-white" />
                          ) : (
                            <Search className="w-4 h-4 text-white" />
                          )}
                          Search
                        </button>
                      </div>

                      {addonSearchResults.length > 0 && (
                        <div className="border border-mcdeploy-border/60 rounded-lg max-h-[360px] overflow-y-auto divide-y divide-mcdeploy-border/40 bg-mcdeploy-bg/40 p-1">
                          {addonSearchResults.map(addon => (
                            <div key={addon.id} className="p-3 flex items-center justify-between gap-3 text-sm hover:bg-mcdeploy-border/20 rounded transition duration-150">
                              <div className="flex items-center gap-3 min-w-0 flex-1">
                                {addon.logoUrl ? (
                                  <img src={addon.logoUrl} alt={addon.name} className="w-10 h-10 rounded object-cover flex-shrink-0 border border-mcdeploy-border" />
                                ) : (
                                  <div className="w-10 h-10 bg-mcdeploy-border/40 rounded flex items-center justify-center text-mcdeploy-muted font-bold text-xs flex-shrink-0">AD</div>
                                )}
                                <div className="min-w-0 flex-1">
                                  <div className="font-bold text-white truncate">{addon.name}</div>
                                  <div className="text-xs text-mcdeploy-muted truncate mt-0.5">{addon.summary}</div>
                                </div>
                              </div>
                              <div className="flex items-center gap-3 flex-shrink-0">
                                <span className="text-xs text-mcdeploy-muted hidden sm:inline">
                                  {addon.downloads.toLocaleString()} downloads
                                </span>
                                <button
                                  type="button"
                                  onClick={() => handleSelectAddon(addon)}
                                  className={`px-3 py-1.5 rounded text-xs font-bold transition duration-150 ${selectedAddon?.id === addon.id ? 'bg-mcdeploy-green text-white' : 'bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-mcdeploy-muted hover:text-white'}`}
                                >
                                  {selectedAddon?.id === addon.id ? 'Selected' : 'Select'}
                                </button>
                              </div>
                            </div>
                          ))}
                        </div>
                      )}
                    </div>

                    {/* VERSION & INSTALL PANEL */}
                    <div className="lg:col-span-5 bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-5 shadow-lg flex flex-col min-h-[300px]">
                      <h3 className="text-sm font-bold text-white flex items-center gap-2 border-b border-mcdeploy-border/40 pb-3 mb-4">
                        <Download className="w-4 h-4 text-mcdeploy-green" /> Addon Installation
                      </h3>

                      {selectedAddon ? (
                        <div className="space-y-4 flex-1 flex flex-col">
                          {/* Selected Addon Header */}
                          <div className="flex items-center gap-3 p-3 bg-mcdeploy-bg/40 border border-mcdeploy-border/60 rounded-lg">
                            {selectedAddon.logoUrl ? (
                              <img src={selectedAddon.logoUrl} alt={selectedAddon.name} className="w-12 h-12 rounded object-cover border border-mcdeploy-border" />
                            ) : (
                              <div className="w-12 h-12 bg-mcdeploy-border/60 rounded flex items-center justify-center text-mcdeploy-muted font-bold text-sm">AD</div>
                            )}
                            <div className="min-w-0">
                              <h4 className="font-bold text-white text-sm truncate">{selectedAddon.name}</h4>
                              <p className="text-xs text-mcdeploy-muted uppercase font-bold tracking-wider mt-0.5">{selectedAddon.source}</p>
                            </div>
                          </div>

                          {/* Version Select */}
                          <div className="space-y-2 flex-1 flex flex-col min-h-0">
                            <label className="block text-xs font-bold text-mcdeploy-muted uppercase tracking-wider">Select Version</label>
                            
                            {fetchingAddonVersions ? (
                              <div className="flex-1 flex items-center justify-center py-8">
                                <Loader className="w-6 h-6 animate-spin text-mcdeploy-green" />
                              </div>
                            ) : addonVersionsList.length === 0 ? (
                              <div className="p-4 text-center text-xs text-mcdeploy-muted bg-mcdeploy-bg/10 border border-dashed border-mcdeploy-border rounded">
                                No versions found for this addon.
                              </div>
                            ) : (
                              <div className="space-y-2 overflow-y-auto max-h-[220px] pr-1 flex-1 min-h-0">
                                {addonVersionsList.map(v => (
                                  <button
                                    key={v.id}
                                    type="button"
                                    onClick={() => setSelectedAddonVersion(v)}
                                    className={`w-full text-left p-3 rounded border transition duration-150 flex items-center justify-between gap-3 ${selectedAddonVersion?.id === v.id ? 'bg-mcdeploy-green/10 border-mcdeploy-green' : 'bg-mcdeploy-bg border-mcdeploy-border/60 hover:bg-mcdeploy-border'}`}
                                  >
                                    <div className="min-w-0 flex-1">
                                      <div className="font-bold text-white text-xs truncate">{v.name}</div>
                                      <div className="text-[10px] text-mcdeploy-muted mt-1 truncate">
                                        File: <span className="font-mono text-white/90">{v.filename || 'addon.jar'}</span>
                                      </div>
                                      <div className="text-[10px] text-mcdeploy-muted mt-0.5 flex flex-wrap gap-1">
                                        {v.gameVersions && v.gameVersions.slice(0, 3).map((gv, idx) => (
                                          <span key={idx} className="bg-mcdeploy-border/40 text-white px-1 py-0.5 rounded font-mono text-[9px]">{gv}</span>
                                        ))}
                                        {v.loaders && v.loaders.slice(0, 2).map((ld, idx) => (
                                          <span key={idx} className="bg-mcdeploy-green/20 text-mcdeploy-green px-1 py-0.5 rounded capitalize text-[9px] font-semibold">{ld}</span>
                                        ))}
                                      </div>
                                    </div>
                                  </button>
                                ))}
                              </div>
                            )}
                          </div>

                          {/* Install Status Logs */}
                          {addonStatusMsg && (
                            <div className={`p-3 rounded text-xs font-semibold font-mono ${addonStatusMsg.startsWith('Error') ? 'bg-red-950/40 border border-red-900/50 text-red-400' : 'bg-mcdeploy-green/10 border border-mcdeploy-green/20 text-green-400'}`}>
                              {addonStatusMsg}
                            </div>
                          )}

                          {/* Download / Install Button */}
                          <button
                            type="button"
                            onClick={() => handleInstallAddon(selectedAddonVersion)}
                            disabled={installingAddon || !selectedAddonVersion}
                            className="w-full bg-mcdeploy-green hover:bg-mcdeploy-darkgreen disabled:opacity-50 disabled:cursor-not-allowed text-white py-3 rounded text-sm font-bold transition duration-150 flex items-center justify-center gap-1.5 shadow-md shadow-mcdeploy-green/10"
                          >
                            {installingAddon ? (
                              <>
                                <Loader className="w-4 h-4 animate-spin" />
                                Downloading...
                              </>
                            ) : (
                              <>
                                <Download className="w-4 h-4" />
                                Install Selected Version
                              </>
                            )}
                          </button>
                        </div>
                      ) : (
                        <div className="flex-1 flex flex-col items-center justify-center text-center p-6 bg-mcdeploy-bg/10 border border-dashed border-mcdeploy-border/60 rounded-lg">
                          <Puzzle className="w-12 h-12 text-mcdeploy-muted mb-3 animate-pulse" />
                          <h4 className="font-bold text-white text-sm">No Addon Selected</h4>
                          <p className="text-xs text-mcdeploy-muted mt-1 max-w-xs">
                            Search for an addon on the left and select it to inspect versions and install.
                          </p>
                        </div>
                      )}
                    </div>
                  </div>
                </div>
              )}

            </div>
          )}

        </div>
        
        {/* Footer copyright */}
        <footer className="h-14 border-t border-mcdeploy-border flex items-center justify-center bg-mcdeploy-card text-xs text-mcdeploy-muted font-medium">
          Â© MCDeploy - Minecraft Server Management
        </footer>
      </main>

      {showDeleteModal && (
        <div className="fixed inset-0 bg-black/75 backdrop-blur-sm z-50 flex items-center justify-center p-4">
          <div className="bg-mcdeploy-card border border-mcdeploy-border p-6 rounded-lg max-w-md w-full shadow-2xl space-y-6">
            <div className="flex items-center gap-3 text-red-500">
              <AlertTriangle className="w-8 h-8" />
              <h4 className="text-lg font-bold text-white font-sans">Permanently Delete Server?</h4>
            </div>
            <p className="text-sm text-mcdeploy-muted leading-relaxed font-sans">
              Are you sure you want to delete <span className="text-white font-semibold">{showDeleteModal.name}</span>? 
              This action is irreversible. All world data, configuration files, and backups will be permanently deleted, and the network port <span className="text-white font-mono">{showDeleteModal.port}</span> will be freed.
            </p>
            
            <div className="space-y-2 font-sans">
              <label className="block text-xs font-semibold text-mcdeploy-muted uppercase tracking-wider">
                Type the server name to confirm: <span className="text-red-400 font-mono select-none">{showDeleteModal.name}</span>
              </label>
              <input 
                type="text" 
                value={deleteConfirmName}
                onChange={(e) => setDeleteConfirmName(e.target.value)}
                placeholder="Enter server name..."
                className="w-full bg-mcdeploy-bg border border-mcdeploy-border text-white px-3 py-2 rounded focus:outline-none focus:border-red-500 font-mono text-sm"
              />
            </div>

            <div className="flex items-center justify-end gap-3 font-sans">
              <button 
                onClick={() => { setShowDeleteModal(null); setDeleteConfirmName(''); }}
                className="px-4 py-2 bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-white rounded text-sm transition font-medium"
              >
                Cancel
              </button>
              <button 
                onClick={() => {
                  handleDeleteServer(showDeleteModal.uuid);
                  setShowDeleteModal(null);
                  setDeleteConfirmName('');
                }}
                disabled={deleteConfirmName !== showDeleteModal.name}
                className="px-4 py-2 bg-red-600 hover:bg-red-700 disabled:opacity-50 disabled:cursor-not-allowed text-white rounded text-sm transition font-bold"
              >
                Permanently Delete
              </button>
            </div>
          </div>
        </div>
      )}

      {showSearchModal && (
        <div className="fixed inset-0 bg-black/85 backdrop-blur-sm z-50 flex items-center justify-center p-4">
          <div className="bg-mcdeploy-card border border-mcdeploy-border w-full max-w-5xl h-[85vh] rounded-lg shadow-2xl flex flex-col overflow-hidden animate-in fade-in zoom-in-95 duration-150">
            
            {/* Modal Header */}
            <div className="flex items-center justify-between p-6 border-b border-mcdeploy-border">
              <div>
                <h4 className="text-lg font-bold text-white">Search Results - Grid View</h4>
                <p className="text-xs text-mcdeploy-muted mt-1">
                  Showing matches for "{modpackQuery}" on {modpackSource === 'modrinth' ? 'Modrinth' : 'CurseForge'}. Scroll to load more results.
                </p>
              </div>
              <button 
                onClick={() => setShowSearchModal(false)}
                className="text-mcdeploy-muted hover:text-white bg-mcdeploy-border/40 hover:bg-mcdeploy-border px-3 py-1.5 rounded transition text-sm font-semibold"
              >
                Close
              </button>
            </div>

            {/* Modal Grid Body */}
            <div 
              onScroll={(e) => {
                const { scrollTop, clientHeight, scrollHeight } = e.target;
                if (scrollHeight - scrollTop - clientHeight < 50) {
                  handleLoadMoreModpacks();
                }
              }}
              className="flex-1 overflow-y-auto p-6"
            >
              <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 gap-5">
                {modalModpacks.map(pack => {
                  const isSelected = selectedModpack?.id === pack.id;
                  return (
                    <div 
                      key={pack.id} 
                      className={`bg-mcdeploy-bg border p-4.5 rounded-lg flex flex-col justify-between gap-4 transition duration-150 ${isSelected ? 'border-mcdeploy-green ring-1 ring-mcdeploy-green/30' : 'border-mcdeploy-border hover:border-mcdeploy-green/50'}`}
                    >
                      <div className="flex gap-3">
                        {pack.logoUrl ? (
                          <img src={pack.logoUrl} alt={pack.name} className="w-12 h-12 rounded object-cover flex-shrink-0 border border-mcdeploy-border" />
                        ) : (
                          <div className="w-12 h-12 bg-mcdeploy-border/60 rounded flex items-center justify-center text-mcdeploy-muted font-bold text-sm flex-shrink-0">MP</div>
                        )}
                        <div className="min-w-0">
                          <h5 className="font-bold text-white text-sm truncate">{pack.name}</h5>
                          <span className="text-[10px] text-mcdeploy-muted capitalize font-mono px-1.5 py-0.5 bg-mcdeploy-border rounded inline-block mt-1">
                            {pack.source}
                          </span>
                          <span className="text-[10px] text-mcdeploy-muted font-semibold ml-2">
                            {pack.downloads.toLocaleString()} dl
                          </span>
                        </div>
                      </div>

                      <p className="text-xs text-mcdeploy-muted line-clamp-2 h-8 leading-relaxed">
                        {pack.summary}
                      </p>

                      <button
                        type="button"
                        onClick={() => {
                          handleSelectModpack(pack);
                          setShowSearchModal(false);
                        }}
                        className={`w-full py-2 rounded text-xs font-bold transition duration-150 ${isSelected ? 'bg-mcdeploy-green text-white' : 'bg-mcdeploy-border hover:bg-mcdeploy-border/80 text-mcdeploy-muted hover:text-white'}`}
                      >
                        {isSelected ? 'Selected' : 'Select Modpack'}
                      </button>
                    </div>
                  );
                })}
              </div>

              {/* Loader */}
              {modalLoading && (
                <div className="py-6 flex items-center justify-center gap-2 text-mcdeploy-muted text-sm italic">
                  <Loader className="w-5 h-5 animate-spin text-mcdeploy-green" /> Loading more modpacks...
                </div>
              )}

              {!modalHasMore && modalModpacks.length > 0 && (
                <div className="py-6 text-center text-xs text-mcdeploy-muted italic">
                  No more results to show.
                </div>
              )}

              {modalModpacks.length === 0 && !modalLoading && (
                <div className="py-12 text-center text-mcdeploy-muted italic text-sm">
                  No results found for your query.
                </div>
              )}
            </div>

          </div>
        </div>
      )}

      {/* Global toast stack + confirm modal (portal-like â€” renders on top of everything) */}
      <ToastStack toasts={toasts} onDismiss={(id) => setToasts(prev => prev.filter(t => t.id !== id))} />
      <ConfirmDialog dialog={confirmDialog} />
    </div>
  );
}
