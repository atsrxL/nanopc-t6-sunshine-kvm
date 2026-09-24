# Mouse mode wire contract — client implementation

Client implementation uses these exact field names. Server HTTP integration must
match them before Windows release; the client will not infer capability from USB
availability or from the first movement packet.

- Authenticated GET /serverinfo: XML element RKMoonMouseModes, comma-separated
  lowercase tokens. Relative-only configuration: relative. Explicitly enabled,
  validated dual HID configuration: relative,absolute. No spaces required.
- Both /launch and /resume query parameter: rkmoonMouseMode=relative or
  rkmoonMouseMode=absolute. Exactly one value; client removes duplicate values
  before appending the selected mode. Mode is immutable for the session/lease.
- Server validates the requested mode against configured capabilities before
  accepting launch/resume and before any button, wheel, movement or keyboard input
  can acquire an input lease. Invalid/unsupported mode must reject, never downgrade.
- For legacy clients with no parameter, server may retain relative as its default;
  this client always sends the parameter. Missing capability metadata permits only
  explicitly selected relative mode for legacy compatibility; absolute always requires
  an explicit advertised absolute token. Unknown nonempty capabilities enable neither.
- Internal HID hello already observed in shared implementation uses mouse_mode with
  relative|absolute. HTTP integration must propagate the validated selection there.
- Absolute motion uses standard encrypted GameStream absolute-position packets.
  LiSendMousePositionEvent gets video-region x/y clamped to [0,width-1]/[0,height-1]
  with reference width/height. Moonlight serializes reference width-1/height-1;
  server must use the on-wire reference maxima for normalization, not subtract again.
- Native touch feature must remain unadvertised unless implemented. Absolute client
  touch otherwise becomes mapped absolute mouse input. Relative touch stays trackpad.
- CtrlAltShiftC controls local visibility only. GUI mode changes apply between sessions.

Server environment flag/header ownership remains with parent/server agent. The exact
flag name is not a client dependency. Hardware dual-HID readiness alone does not
constitute HTTP capability/launch integration acceptance. Await server confirmation.

Parent confirmed capability field implementation: relative,absolute behind explicit
server allow flag, otherwise relative. HTTP launch/resume propagation still pending
server-agent confirmation.

Touch policy correction: dedicated client always emulates absolute mouse for direct
touch, even if upstream host flags claim native pen/touch. Relative touch remains
trackpad gestures. No native touch protocol support is claimed.

Parent confirmed rkmoonMouseMode contract fixed; Windows freeze proceeds independently
of production server deployment. Client absolute touch is forced mouse emulation.
