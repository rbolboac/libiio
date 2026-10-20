# libiio on no-OS — architecture

How libiio v1.0 is wired into the ADI no-OS framework, layer by layer, with
`file:line` citations so every claim can be re-verified. Line numbers drift; the
surrounding function names do not, so grep the name if a number no longer matches.

---

## 1. What libiio v1.0 expects from a backend

A backend is a `struct iio_backend` wrapping a `struct iio_backend_ops` table
(`include/iio/iio-backend.h`). The build defines `WITH_EXTERNAL_BACKEND=1`
(`CMakeLists.txt:109`), which makes libiio reference an undefined symbol
`iio_external_backend` and lets the linker pull in whatever provides it. This port
provides it at `backend.c:350-356`:

```c
const struct iio_backend iio_external_backend = {
	.name = "no-os",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 0,
	.uri_prefix = "no-os:",
	.ops = &noos_ops,
};
```

So `iio_create_context(&params, "no-os:")` reaches `noos_create_context`.

Errors are encoded into pointers, kernel-style: `iio_ptr(-ENOMEM)` to return an
error as a pointer, `iio_err(ptr)` to extract it (0 means a real pointer),
`iio_err_cast(ptr)` to forward one. Any op returning a pointer uses this.

## 2. Layer map

```
transport     iiod/{uart,network,usb}.c     bytes in/out, handshake, main loop
protocol      iiod/responder.c              v1 binary opcode dispatch
backend       backend.c                     iio_backend_ops -> callback table
table         backend_table.c               channel table -> attributes, values, scans
standard ABI  include/iio_{adc,accel,temp}.h  the standard channels of each type
driver        a part's driver               its channel table, values, read_raw
```

Each layer knows only the one below it. Concretely, and worth preserving:
`backend.c` contains no ADC knowledge, the table layer knows no device
type, and a driver contains no string formatting or parsing.

**A device is described as a Linux driver describes it**: one table of
channels (`struct noos_iio_chan`, Linux's `iio_chan_spec`) and one set of
handlers (`read_raw`, `write_raw`, `read_avail`, `read_scan`, Linux's
`iio_info`), on the device itself (§4). There are no device classes,
capability structs or channel groups: a part with an accelerometer and a
temperature lists both in its one table and switches on the channel type
in its `read_raw`, as Linux's `adxl355_core.c` does.

Two things keep the drivers short and alike:

- **the standard channels of a type** (`include/iio_accel.h`,
  `iio_temp.h`, `iio_adc.h`, §7b): macros that write a channel the way
  every part of that type has it. The driver names the standard
  attributes its part has; the macro puts each one where the type has it
  (an accelerometer's scale and sampling frequency shared by type, an ADC's
  sampling frequency on the device), so every accelerometer, temperature and
  ADC shows the same ABI;
- **values as data** (`struct noos_iio_value`, §4): a standard attribute
  that is a constant (a scale) or picked from a table (a sampling
  frequency, a filter corner) is one line, which the table layer serves
  itself, list and write check included. `read_raw`/`write_raw` are left
  with what needs the hardware: raw readings, calibrations.

A type with no header yet (a DAC, a PLL, an IMU) writes its
`noos_iio_chan` entries directly, which is all the macros do.

Helpers: `iio_enum.c` (`include/iio_enum.h`), `struct noos_iio_enum` with
`NOOS_IIO_ENUM(name, &e)`, an `ext` attribute that takes one of a list of
names, as Linux's `IIO_ENUM` (the MAX32 ADC's `reference`); its
`mask` says which names the part has, the others are `-ENOTSUP`;
`NOOS_IIO_ENUM_AVAILABLE` lists them.

Trigger: `trigger/iio_trigger_timer*.c` (§7a), used by the sample's ADC.

## 3. Context construction

`noos_create_context` (`backend.c:266-325`):

1. `iio_context_create_from_backend(params, &iio_external_backend,
   NOOS_BACKEND_VERSION, 1, 0, 0, "v1.0")`
2. for each entry in `noos_iio_devices[]`:
   - the host-visible id: `"trigger%u"` when `info->is_trigger`, else
     `"iio:device%u"`, each with its own counter. The prefix is not cosmetic:
     `iio_device_is_trigger()` (`device.c`) requires an id starting with
     `trigger` **and** zero channels, and `iio_rwdev`/`iio_info` rely on it
   - `iio_context_add_device(ctx, id, info->name, NULL)`
   - `iio_device_set_pdata(iio_dev, (struct iio_device_pdata *)info)` — **this is
     the load-bearing line.** Every later op recovers its callback table with
     `iio_device_get_pdata(dev)`; there is no other link from an `iio_device` back
     to the no-OS device.
   - `info->add_channels(info->dev, iio_dev)` if present; a failure is logged
     with `pr_err` and the device is kept (a trigger uses the same hook to add
     its device attributes)
   - if the device ended up with channels: `iio_device_add_buffer(iio_dev, 0)`,
     `iio_buffer_set_direction(buf, info->direction ? "out" : "in")`, then one
     `iio_buffer_add_scan_element(buf, chn, NULL)` per channel for which
     `iio_channel_is_scan_element(chn)` holds.

The scan-element loop is not optional. Scan elements became buffer-owned upstream,
and without registering them the buffer reports zero of them, which is what
`iio_rwdev` uses to compute the sample size — it fails with a trigger timeout or
zero-length reads. `NULL` is passed for `en_path` because there is no sysfs enable
file on bare metal.

## 4. Device registry

`include/iio_device.h`. A device is described by value:

```c
struct noos_iio_device_info {
	const char *name;
	void *dev;
	int direction;                          /* 0 = input (RX), 1 = output (TX) */
	noos_iio_add_channels_t add_channels;
	noos_iio_read_attr_t read_attr;
	noos_iio_write_attr_t write_attr;
	noos_iio_read_samples_t read_samples;
	noos_iio_write_samples_t write_samples;
	noos_iio_enable_buffer_t enable_buffer;
	noos_iio_reg_read_t reg_read;
	noos_iio_reg_write_t reg_write;
	noos_iio_get_scan_format_t get_scan_format;  /* formats that change */
	bool is_trigger;                        /* id "triggerN", no channels */
	const char *trigger;                    /* name of the trigger pacing it */
};
```

Fill it with a designated initializer (every driver in the tree does,
`*info = (struct noos_iio_device_info) { ... }`), so fields a device does not
use are zero.

The callback typedefs (`:26-58`) take the device as a plain `void *dev`. The
sample path carries the channel information a capture needs:

```c
typedef int (*noos_iio_read_samples_t)(void *dev,
		const struct iio_device *iio_device,
		const struct iio_channels_mask *mask, void *data, size_t bytes);
typedef int (*noos_iio_enable_buffer_t)(void *dev, bool enable);
```

`mask` is the buffer's enabled set (§6), `bytes` a whole number of scans.
`enable_buffer` is optional and is called only on a real state change.

`get_scan_format` is for a device whose scan formats depend on its
configuration (a differential input is signed, an oversampled one wider). It
fills in a scan element's current format and backs the `refresh_format` op
(§5); without it, the formats `add_channels` gave are final.

### Table-driven devices

Instead of `add_channels`/`read_attr`/`write_attr`/`read_samples`, a device can
describe its channels in a table and handle only values, the way a Linux IIO
driver fills an `iio_chan_spec` and implements `read_raw`. The generic callbacks
are in `backend_table.c`; `noos_iio_register_device()` checks the table and
installs the ones the device left NULL.

```c
struct noos_iio_device_info {
	...
	/* as Linux's iio_dev + iio_info; the callbacks get dev */
	const struct noos_iio_chan *channels;
	unsigned int num_channels;
	const struct noos_iio_ext_attr *attrs;	/* device attributes */
	const struct noos_iio_value *values;	/* standard attributes as data */
	unsigned int nb_values;
	read_raw, read_raw_multi, write_raw, write_raw_get_fmt, read_avail;
	read_scan, wait_scan;			/* + enable_buffer, get_scan_format */
};

struct noos_iio_chan {
	/* what it is, as Linux's iio_chan_spec: gives the id */
	enum noos_iio_chan_type type;		/* NOOS_IIO_VOLTAGE ... */
	int channel, channel2;			/* number; modifier or other input */
	bool indexed, modified, differential;
	const char *id;				/* an id of its own instead */
	long scan_index;			/* -1: not one */
	bool output;
	struct noos_iio_scan_type scan_type;	/* scan elements, as Linux's */
	/* NOOS_IIO_INFO_BIT()s, each in one of them, as on Linux */
	uint64_t info_mask_separate, info_mask_separate_available;
	uint64_t info_mask_shared_by_type, info_mask_shared_by_type_available;
	uint64_t info_mask_shared_by_dir, info_mask_shared_by_dir_available;
	uint64_t info_mask_shared_by_all, info_mask_shared_by_all_available;
	const struct noos_iio_ext_attr *ext;	/* other attributes, as text */
	intptr_t priv;				/* the driver's */
};

struct noos_iio_value {				/* one line each: */
	enum noos_iio_chan_type chan_type;	/* NOOS_IIO_CONST(ACCEL, SCALE, */
	enum noos_iio_info info;		/*   INT_PLUS_NANO, 0, 38245)   */
	int type;				/* NOOS_IIO_LIST(ACCEL, SAMP_FREQ, */
	const int *vals; unsigned int nb;	/*   INT_PLUS_MICRO, odrs,      */
	const int *(*avail)(void *dev, unsigned int *nb); /* get, set)  */
	int (*get)(void *dev);			/* NOOS_IIO_LIST_FN(...): a */
	int (*set)(void *dev, unsigned int idx); /*  table that changes   */
};
```

The description follows Linux's so a driver ported from Linux, or written
from its sysfs ABI, carries its `iio_chan_spec` and `read_raw` over almost
as they are, and a client sees the same device either way. The enums
(`noos_iio_info`, `noos_iio_val`, `noos_iio_chan_type`, `noos_iio_modifier`)
are Linux's, in Linux's order, with `NOOS_` in front.

- **scan type:** a scan element's `scan_type` is Linux's `struct
  iio_scan_type`, field for field (`sign`, `realbits`, `storagebits`,
  `shift`, `repeat`, `endianness` with `NOOS_IIO_BE`/`_LE`/`_CPU`), so a Linux
  driver's carries over as it is. The table layer turns it into libiio's
  format when it creates the channel (`le:s20/32>>0`); the scale clients
  convert with is the channel's `scale` attribute, which libiio reads when it
  creates the context. Registration refuses a scan element with no sign,
  storage that is not 8 to 64 bits in whole bytes, or data and shift wider
  than it.
- **ids:** made as Linux names the channel: `voltage0`, `voltage0-voltage1`
  (differential, must be indexed), `accel_x` (modified), `anglvel0_z`
  (both), `temp` (neither); `id` overrides it.
- **attributes:**
  - each `NOOS_IIO_INFO_*` bit gives the standard attribute (all 32 of
    Linux's: `raw`, `input`, `scale`, `offset`, `calibscale`, `calibbias`,
    `peak_raw`, `mean_raw`, `sampling_frequency`, `frequency`, `phase`,
    `hardwaregain`, `en`, `thermocouple_type`, ...), plus
    `<name>_available` when the bit is also in the mask's `_available` one;
  - the mask a bit is in places it where Linux does, as libiio's local
    backend shows it: `separate` and `shared_by_type` ones are attributes
    of the channel (file names `in_accel_x_scale`, `in_accel_scale`),
    `shared_by_dir` ones are device attributes `in_<name>`/`out_<name>`,
    `shared_by_all` ones device attributes `<name>`. A device attribute
    several channels have is created once and served through the first of
    them, as on Linux;
  - a `noos_iio_value` entry serves its attribute on every channel of its
    `chan_type`, before `read_raw` sees it: a read gives the row `get`
    says, a write is parsed as the entry's `type` and must be a row of the
    table (else `-EINVAL`; `-EPERM` without `set`), and `_available` lists
    the table;
  - otherwise `read_raw` returns any of Linux's `IIO_VAL_*` (same numbers):
    INT, INT_PLUS_MICRO(_DB), INT_PLUS_NANO, INT_64 (low, high halves),
    FRACTIONAL, FRACTIONAL_LOG2, CHAR, and INT_MULTIPLE from
    `read_raw_multi`. The backend prints them as Linux's
    `__iio_format_value()` does, digit for digit, and parses writes as
    `write_raw_get_fmt` says (micro by default, which also takes plain
    integers; a dB value may end in `dB`);
  - `read_avail` returns a list or a `[min step max]` range;
  - anything else is a `noos_iio_ext_attr` on the channel, or in the
    device's `attrs`, read and written as text.
- **checks:** `noos_iio_register_device()` refuses (`-EINVAL`) a channel
  with a bit in two masks, an `_available` bit without its attribute, a bit
  past `NOOS_IIO_INFO_COUNT`, an id it cannot make, an attribute neither a
  value entry nor `read_raw` serves, a list neither a value entry nor
  `read_avail` serves, a value entry no channel of its type has or that is
  given twice, and a writable value of a type a write cannot be matched
  against (FRACTIONAL). `tests/test_table.c` checks all of the above on
  the host.
- **buffers:**
  - `wait_scan`, if set, paces each scan (the ADXL355 waits for
    `DATA_RDY`), then `read_scan(dev, enabled, int64_t *vals)` gives a
    value per enabled channel, `vals[i]` for bit `i`, the channel's index
    in the table;
  - the backend lays the sample out as `iio_device_get_sample_size()` does
    (channel order, each element aligned to its own size, shared scan indexes
    sharing storage) from each channel's *current* format, so a format that
    `get_scan_format` changed packs without driver code;
  - signed values are stored sign-extended over the whole element, unsigned
    ones masked to their bits, both at their shift.
- **buffer and formats:** a buffer enable or disable reaches
  `enable_buffer`; `refresh_format` asks `get_scan_format`, and a device
  without one has static formats.
- **scope:** at most 32 channels, input only (`read_scan`), no `repeat > 1`.
  A device that needs more keeps the callbacks; any callback it sets wins
  over the generic one.

`noos_iio_chan_of(chn)` gives the table entry a channel was created from, for
`get_scan_format` and the `ext` callbacks. An `ext` entry carries a `priv`
of its own, passed to its callbacks, so one handler serves several
attributes (`noos_iio_enum`, §2).

`noos_iio_register_device()` (`backend.c:186-196`) copies the struct into
`noos_iio_devices[NOOS_IIO_MAX_DEVICES]` (16) and bumps `noos_iio_device_count`.
Registration must happen **before** `iio_create_context`, because the context is
built from a snapshot of that table. `samples/iiod/main.c:noos_register_devices`
registers the ADC (named after the part, `adc-demo` without hardware), then the
`timer0` trigger, then the snake game.

Still not carried: events.

## 5. Op-by-op mapping

`noos_ops` fills 17 of the `iio_backend_ops` slots.

| op | function | behaviour | gap |
|---|---|---|---|
| `create` | `:267` | see §3 | — |
| `read_attr` | `:199` | dispatch to `info->read_attr` | — |
| `write_attr` | `:212` | same shape | — |
| `get_trigger` | `:225` | `iio_context_find_device(ctx, info->trigger)`, `-ENODEV` when the device names none | `set_trigger` (`:247`) accepts only the trigger `get_trigger` returns (`-EINVAL` otherwise): the pairing is fixed at build time |
| `open_buffer` | `:34` | allocates `struct iio_buffer_pdata { dev; mask; enabled; }` and **stores the mask** (§6) | — |
| `close_buffer` | `:72` | disables the buffer first if the client never did (dropped connection), then `free` | — |
| `enable_buffer` | `:55` | calls `info->enable_buffer` on a real state change, tracks `enabled` | — |
| `cancel_buffer` | `:81` | empty | in-flight transfers cannot be aborted |
| `readbuf` / `writebuf` | `:85` / `:105` | `read_samples` / `write_samples` with `(info->dev, dev, mask, data, len)` | — |
| `create_block` / `free_block` | `:127` / `:147` | zalloc the pdata + malloc the payload | — |
| `enqueue_block` | `:153` | **performs the whole transfer inline**, with the mask, and stashes the result in `pdata->error` | not async |
| `dequeue_block` | `:181` | `return pdata->error;` | ignores `nonblock`, no `-EBUSY`, no double-op detection |
| `reg_read` / `reg_write` | `:242` / `:254` | `-ENOSYS` when the device hook is NULL | — |
| `refresh_format` | `noos_refresh_format` | the device's `get_scan_format` for a scan element, keeping the cached `scale`/`offset` as the client backends do; a device without the hook succeeds unchanged; `-EBUSY` while a buffer of the device is open, because queued blocks used the old layout | — |

Ordering worth knowing: `iio_stream` (and so `iio_rwdev`) enqueues its blocks
**before** it enables the buffer. Because enqueue is synchronous here, the first
blocks are filled while `enabled` is still false — so nothing may depend on
`enable_buffer(true)` having run first. The trigger therefore starts lazily on
the first sample wait, and stops on `enable_buffer(false)` (§7).

The libiio contract these deviate from, for when the async work is picked up:
enqueue is documented as asynchronous (`iio.h:1291-1310`);
`dequeue_block(nonblock=true)` should return `-EBUSY` when the block is not ready
(`iio.h:1308-1310`); double enqueue or double dequeue conventionally returns
`-EPERM` (`local-mmap.c:191-194`, `:228-231`). Today enqueue-then-dequeue works because the
transfer completes inside enqueue, so the deviation is invisible to a
well-behaved client and fatal to one that polls.

## 6. Channel masks

```c
struct iio_channels_mask {
	size_t words;
	uint32_t mask[];
};
```
(`iio-private.h:185-188`)

`iio_channel_is_enabled(chn, mask)` (`channel.c:503-506`) tests bit `chn->number`.
`number` is the channel's index in `dev->channels[]` **after sorting**
(`device.c:565`) — not its id, and not the `long index` passed to
`iio_device_add_channel`. Resolving a mask bit therefore means walking
`iio_device_get_channel(dev, i)` in order.

The mask handed to `open_buffer` is a private copy owned by the `buf_stream`:
allocated at `buffer.c:145-153`, passed at `:167`, freed only in
`iio_buffer_close` (`:196`). Consequences:

- the pointer is stable for the buffer's lifetime, so a backend **may store it**
  without copying, which is what `backend.c` does
- it is fully populated before `open_buffer` is called
- it is non-`const` on purpose: a backend may write bits back for channels the
  hardware forces on, e.g. coupled pairs (`local.c:1763-1773`)
- changing the enabled set requires close + reopen

For bytes-per-scan, use `iio_device_get_sample_size(dev, mask)`
(`device.c:368-407`) rather than hand arithmetic: it uses `format.length` (the
storage size) not `format.bits` (the meaningful bits), collapses channels sharing
an `index`, aligns each element to its own length, and pads the total.

## 7. `samples/iiod/adc/` — the sample's ADC

One ADC driver per image, the one the `ADC` cache variable picks
(`samples/iiod/adc/${ADC}/${ADC}.c`): `common_api` on the MAX32 parts,
`adc_demo` everywhere else. Each is an ordinary table-driven device with the
standard ADC channels (`iio_adc.h`, §7b), like any other driver, and gives
itself to `main.c` through `sample_adc.h`:

```c
const struct sample_adc_param param = {
	.trigger = IIO_TRIGGER_TIMER_NAME,	/* paces the buffer, §7a */
	.wait_scan = iio_trigger_timer_wait,
	.stop_scan = iio_trigger_timer_stop,
};

sample_adc_get_device_info(&param, &info);	/* brings the ADC up */
noos_iio_register_device(&info);
```

Both have, per input, `raw` and `scale`: one LSB in mV, the reference over
2^bits (`NOOS_IIO_VAL_FRACTIONAL_LOG2`), so `raw * scale` is the input in
mV as on Linux, and the same value the scan format carries. `scale` is
read-only (`-EPERM`): it follows the reference. Neither has device
attributes; the sampling frequency is the timer trigger's (§7a).

- **adc_demo** (`adc-demo`, no hardware): `voltage0` and `voltage1`, 16-bit
  over 2.5 V (`be:u16/16`, scale 0.038146972). `voltage0` plays a 32-step
  sine, `voltage1` the same a quarter period on. The waveform moves one step
  per scan; `raw` gives the current sample and does not move it. The scale
  is one `NOOS_IIO_CONST` value; the whole driver is about 130 lines.
- **common_api** (`max32690-adc`, `max32655-adc`, ... after the part): the
  MCU's own ADC, `voltage0` ... `voltage<IIO_ADC_NUM_CHANNELS - 1>` (1 by
  default), 12-bit on revB, 10-bit on revA (`be:u12/16`, `be:u10/16`).
  `raw` is one conversion (§8). On revB the reference can be switched:
  `reference` and `reference_available`, one value for the ADC whichever
  input it is set through; the scale follows it.

### The reference names — do not re-litigate this

`reference` takes the shared names of ADC references, by number: `VDD`,
`VDD/2`, `VDD/3`, `VDD/4`, `Internal`, `External0`, `External1`
(`maxim_adc_references[]`). They are **a namespace, not a capability
list**: their job is to give every possible reference one agreed name and
number, so that `reference = "Internal"` means the same thing to a host
whatever ADC is behind it. They must therefore **not** be shrunk to match one
part's silicon, which would make the same number mean something different
from one image to the next.

What the part has is the `noos_iio_enum`'s `mask` (§2): `Internal` (the 1.25 V
bandgap), and `External0` when the board gives its voltage
(`IIO_ADC_EXT_REF_VOLTAGE_MV`, so the scale can follow it). A name the part
lacks is `-ENOTSUP`, an unknown one `-EINVAL`, and `reference_available` lists
only what the part has. `MXC_ADC_REF_INT_2V048` exists in silicon but has no
name in the namespace, so it is unreachable — an honest gap, not a reason to
overload `External1`.

### The sample path

`read_scan` converts the enabled inputs once each (`adc_demo` reads its table
instead). `wait_scan` paces each scan with the timer trigger's wait. The
table layer lays the scan out as `iio_device_get_sample_size()` describes.

Enabling only `voltage1` on the demo gives a 2-byte scan of input 1 alone;
enabling both gives `v0 v1 v0 v1 …`. Disabling the buffer calls `stop_scan`,
which stops the trigger; the next stream's first wait restarts it.

What it still does not do: per-channel chunked capture through the sequencer.
Each value is one conversion, which is what makes the timer pacing meaningful
(§7a).

## 7a. `trigger/iio_trigger_timer.c` — the sampling trigger

Why it exists: without it `read_samples` converts back-to-back, as fast as the
CPU allows, so the interval between samples depends on the ADC, the bus and the
number of enabled channels — the data is fast but its time axis is meaningless.
The trigger makes each scan start on a hardware-timer tick, so samples are evenly
spaced at a known rate.

Device, as the host sees it:

```
trigger0: timer0
	1 device-specific attributes found:
		attr  0: sampling_frequency value: 1000
```

and the ADC reports `trigger0` as its trigger (`iio_info` shows it; over the
wire the responder's `GETTRIG` maps `noos_get_trigger`'s result to a device
index).

Mechanics. `iio_trigger_timer.c` holds the platform-independent part: ticks,
wait, attribute. The hardware tick sits behind `iio_trigger_timer_hw.h`
(`_hw_init`, `_hw_start(freq_hz, tick)`, `_hw_stop`).
`trigger/iio_trigger_timer_no_os.c` implements it with no-OS's timer and IRQ
API and the `TRIGGER_*` wiring of `timers.h`, on every platform. Three
optional `timers.h` defines cover a no-OS layer that differs from Maxim's
(the aducm3029 one, §13):
- `TRIGGER_IRQ_HANDLE_TIMER`: the callback handle is the timer descriptor;
- `TRIGGER_IRQ_PRIORITY_ID`: the id `no_os_irq_set_priority()` takes;
- `TRIGGER_TIMER_MAX_TICKS`: a rate whose period does not fit the counter is
  refused rather than truncated.

- `iio_trigger_timer_init` — called by the sample before `iio_adc_init` — runs `_hw_init`
  (no-OS: sets up the IRQ controller and fills in the `NO_OS_EVT_TIM_ELAPSED`
  callback). The ISR only does `ticks++`.
- `iio_trigger_timer_wait` — returns at once when `sampling_frequency` is 0
  (free running, the old behaviour). Otherwise starts the timer on first use,
  spins until `ticks` changes, then sets `seen = ticks`. In the no-OS backend
  `ticks_count = TRIGGER_TIMER_FREQ_HZ / freq_hz`, and start order is `no_os_timer_init` →
  `no_os_irq_register_callback` → `no_os_irq_enable` → `no_os_timer_start`,
  as `iiod/usb.c` does for its tick: on Maxim, registering the callback is what
  sets the timer's interrupt enable, and `no_os_timer_init` resets the timer
  (`MXC_TMR_Shutdown` + `MXC_TMR_Init`). Registering first leaves the timer
  running without interrupts and the wait spins forever on the first buffer. A scan slower than the period **resyncs**
  rather than bursting to catch up, so spacing never collapses.
- The no-OS backend registers and unregisters the callback **with interrupts
  off** (`trigger_cb_register`/`_unregister`). See the §13 gotcha on the shared
  callback-list iterator: without this, a stop racing the USB tick could free
  the tick's callback and freeze the board in a TMR0 interrupt storm.
- `iio_trigger_timer_stop` — stop, disable IRQ, remove timer. Called on buffer
  disable and on every `sampling_frequency` write, so the new rate applies
  from the next wait.
- `sampling_frequency` — Hz, default 1000, range 0..100000
  (`IIO_TRIGGER_TIMER_{DEFAULT,MAX}_HZ`).

Deliberate design choices:

- **`sampling_frequency` in Hz, one scan per tick**, rather than a period that
  fills one whole *block* per tick. Per-scan pacing is what gives evenly spaced
  samples; per-block pacing only spaces the blocks.
  `sampling_frequency` is also the attribute name of the Linux
  `iio-trig-hrtimer`, so host tooling already knows it.
- **Spin, not work queue.** `NO_THREADS=1` (§10): the wait blocks the
  interpreter for `nb_samples / sampling_frequency` seconds. At 1 kHz a
  100 000-sample block takes 100 s, longer than a client's default timeout —
  raise the rate, use smaller blocks, or write 0.
- **Fixed pairing.** `info->trigger` names the trigger at build time.
  `set_trigger` accepts that trigger and refuses any other, so
  `iio_rwdev -t timer0 -r <Hz>` works: `-r` is a `sampling_frequency` write.

Why no-OS's own `iio/iio_trigger.c` is not reused: `iio_hw_trig`/`iio_sw_trig`
are part of no-OS's *own* IIO server — they take a `struct iio_desc`, and fire
through `iio_process_trigger_type`/`iio_trigger_notify` into that server's
buffer machinery. This port uses libiio's server (`iiod/responder.c`) instead, so
there is no `iio_desc` to hand them. What is reused is the pattern — a
`no_os_timer` + `no_os_irq` callback, as in the ad7091r8-sdz
`iio_timer_trigger_example` — through the same no-OS HAL APIs.

Board wiring lives in `samples/iiod/platform/<platform>/timers.h`, included by `parameters.h`
and by the trigger backend alone (so the driver does not pull in the static UART
and network init structs):

| define | value | used by |
|---|---|---|
| `TRIGGER_TIMER_ID` / `_FREQ_HZ` | 1 / 1 MHz | trigger |
| `TRIGGER_IRQ_ID` / `_HANDLE` / `_PRIORITY` | `TMR1_IRQn` / `MXC_TMR1` / 3 | trigger |
| `USB_TICK_*` | TMR0, priority 3 | USB transport only (`#ifdef NO_OS_USB_TRANSPORT`) |

The Maxim IRQ controller is a singleton, so the trigger and the USB tick share
it and must use different timers. They also share one priority, so neither timer ISR
preempts the other while it walks the callback list.

## 7b. The standard channels: `iio_accel.h`, `iio_temp.h`, `iio_adc.h`

Header-only lists, one per device type, that fix that type's ABI as Linux's
drivers of the type have it. Each macro takes the `NOOS_IIO_BIT()`s the part
has, those of them with a list, and last the scan type (an initializer, as
Linux's, or `NOOS_IIO_NO_SCAN_TYPE`), and places each attribute itself:

| type | channels (priv) | the channel's own | shared |
|---|---|---|---|
| accel | `NOOS_IIO_ACCEL_CHANNEL(X/Y/Z, scan, ...)`, `NOOS_IIO_ACCEL_XYZ(...)`: `accel_x/y/z` (the axis, 0..2) | raw, calibbias, calibscale, ... | `NOOS_IIO_ACCEL_SHARED`: scale, sampling_frequency, the filter corners, oversampling_ratio, by type (`in_accel_scale`) |
| temp | `NOOS_IIO_TEMP_CHANNEL(...)`: `temp`; `NOOS_IIO_TEMP_CHANNEL_N(n, ...)`: `tempN` (n) | everything: raw, input, scale, offset | none |
| adc | `NOOS_IIO_VOLTAGE_CHANNEL(n, ...)`: `voltageN`; `NOOS_IIO_VOLTAGE_DIFF_CHANNEL(p, m, ...)`: `voltageP-voltageM` (n, p) | raw, scale, offset, calibbias, ... | `NOOS_IIO_ADC_SHARED`: sampling_frequency, oversampling_ratio, the device's (device attributes) |

A bit the type does not share is the channel's own, so nothing a part
names is dropped. A part that differs from its type (an ADC with one scale
for all its inputs) writes that `noos_iio_chan` by hand.

## 7c. Putting a part together

`drivers/accel/adxl355/iio_adxl355.c` in no-OS, the whole IIO description
of an ADXL355, ADXL357 or ADXL359:

```c
static const struct noos_iio_chan adxl355_iio_channels[] = {
	NOOS_IIO_ACCEL_XYZ(NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(CALIBBIAS) |
			   NOOS_IIO_BIT(SCALE) | NOOS_IIO_BIT(SAMP_FREQ) |
			   NOOS_IIO_BIT(HIGH_PASS_FILTER_3DB_FREQUENCY),
			   NOOS_IIO_BIT(SAMP_FREQ) |
			   NOOS_IIO_BIT(HIGH_PASS_FILTER_3DB_FREQUENCY),
			   ADXL355_IIO_SCAN_TYPE),
	NOOS_IIO_TEMP_CHANNEL(-1, NOOS_IIO_BIT(RAW) | NOOS_IIO_BIT(SCALE) |
			      NOOS_IIO_BIT(OFFSET), 0, NOOS_IIO_NO_SCAN_TYPE),
};

/* the 357 and 359 differ in their scale and temperature offset */
#define ADXL355_IIO_VALUES(_scale_nano, _temp_offset)			\
	NOOS_IIO_CONST(ACCEL, SCALE, INT_PLUS_NANO, 0, _scale_nano),	\
	NOOS_IIO_LIST(ACCEL, SAMP_FREQ, INT_PLUS_MICRO, adxl355_odr_table, \
		      adxl355_iio_get_odr, adxl355_iio_set_odr),	\
	NOOS_IIO_LIST_FN(ACCEL, HIGH_PASS_FILTER_3DB_FREQUENCY,		\
			 INT_PLUS_MICRO, adxl355_iio_hpf_avail,		\
			 adxl355_iio_get_hpf, adxl355_iio_set_hpf),	\
	NOOS_IIO_CONST(TEMP, SCALE, INT_PLUS_MICRO, -110, 497238),	\
	NOOS_IIO_CONST(TEMP, OFFSET, INT_PLUS_MICRO, _temp_offset, 250000)
```

plus a `read_raw` for `raw` (an axis or the temperature, by the channel's
type) and `calibbias`, a `write_raw` for `calibbias`, `read_scan` (one
reading of the axes) and `wait_scan` (its `DATA_RDY`). The high-pass
corners follow the ODR, so their table is a function's (`LIST_FN`).

## 8. `common_api` and the MSDK

`samples/iiod/adc/common_api/common_api.c` serves every MAX32 part with one
file, because the MSDK's ADC API comes in two revisions that the per-die
`adc.h` tells apart (`MAX_ADC_SLOT_NUM` defined or not, `common_api.h`):

- **revB** (max32690, max32662, max32672, max78002) — slot sequencer plus
  FIFO. `MXC_ADC_Init(&cfg)` takes a config struct. Each conversion does
  `MXC_ADC_Clear_ChannelSelect` → `MXC_ADC_SlotConfiguration` →
  `MXC_ADC_Configuration` → `MXC_ADC_StartConversion()` → spin on
  `MXC_F_ADC_INTFL_SEQ_DONE` with a timeout → `MXC_ADC_DisableConversion` →
  `MXC_ADC_GetData`.
  `MXC_ADC_GetData(int *)` copies **every** word in the FIFO, and back-to-back
  conversions leave more than one there: it gets a `MAX_ADC_FIFO_LEN` buffer
  and the first word is kept. Handing it a single `int` overwrote the caller's
  stack — the sample path's channel table — and the second sample of
  every stream failed with `-EINVAL`.
  12-bit, 1250 mV internal reference. `MXC_ADC_ReferenceSelect` switches
  between `MXC_ADC_REF_INT_1V25` (`Internal`) and `MXC_ADC_REF_EXT`
  (`External0`). No PGA.
- **revA** (max32655, max32650, max32665, max32680, max78000) —
  `MXC_ADC_Init()` takes no argument and `MXC_ADC_StartConversion(channel)`
  *returns the sample*. 10-bit; the reference voltage comes from the board
  (`IIO_ADC_REF_VOLTAGE_MV`, 1220 mV on the max32655), and cannot be
  switched, so there is no `reference` attribute.

The board's `platform.cmake` gives the part's include path and the defines
(`IIO_ADC_NAME`, `IIO_ADC_CLOCK`, `IIO_ADC_REF_VOLTAGE_MV`,
`IIO_ADC_EXT_REF_VOLTAGE_MV`, `IIO_ADC_NUM_CHANNELS`); adding a MAX board adds
no ADC source file.

**Ordering trap on the 690.** `MXC_ADC_Configuration()` calls
`MXC_ADC_SlotsConfig(req)` and then `MXC_ADC_Clear_ChannelSelect()`
(`MaximSDK/.../Source/ADC/adc_me18.c:295-311`). Per-slot
`MXC_ADC_ChSelectConfig()` calls must therefore come **after**
`MXC_ADC_Configuration`, never before. The current one-shot code survives only
because it redoes slot configuration on every read. This is the most likely silent
failure in any future multi-slot sequencer work.

## 9. Known gaps

| gap | where | what solving it takes |
|---|---|---|
| synchronous enqueue; `nonblock` ignored | `backend.c:153-184` | a pending-block list filled by the trigger, with a completion flag `dequeue_block` waits on (`NO_THREADS`, §10) |
| trigger pairing fixed at build time; `set_trigger` only accepts the fixed one | `backend.c:225-257` | triggers as devices a buffer subscribes to |
| `cancel_buffer` cannot abort | `backend.c:81-83` | unsubscribing the buffer from its trigger and waking pending blocks |

Closed since the upstream reset (kept here so nobody re-opens them): channel
mask stored and passed to `read_samples` (§5, §7); mask-filtered interleave of
enabled channels (§7); a real trigger device (§7a).

Specific to this port:

- the sample builds one ADC driver per image (`ADC=`)
- upstream `iiod/responder.c` prints `int32_t` with `%u`, which warns under
  arm-none-eabi; left alone because files outside `no-OS/` track upstream
  exactly
- serial has one client at a time and no disconnect: two contexts on the tty
  (`iio_stresstest -t 2`) interleave on the wire, and a client killed in the
  middle of a block leaves the board sending it to nobody, so the next client
  starts out of sync. Both need a board reset; exit clients with Ctrl-C.
  What a client leaves open between commands is not a problem: the next
  client's `BINARY\r\n` handshake reaches the responder's `reconnect` op,
  which frees the old client's buffers and event streams, so its
  `OPEN_BUFFER` does not meet `-EEXIST`
- latent NULL-deref in `iiod/responder.c:1249-1260` — `iiod_cmd` bounds-checks
  `cmd->op` against `IIOD_NB_OPCODES` but `iiod_op_functions[]` is a sparse
  designated-initializer array, so an unimplemented-but-in-range opcode calls a
  NULL pointer

## 10. Concurrency model

`NO_THREADS=1` (`CMakeLists.txt:106`). Two consequences that shape any async
design:

- `iio_task_token_do_enqueue` runs `iio_task_process` **inline on the calling
  thread** (`task.c:215-216`). There is no worker.
- `iiod/responder.c:330` always calls `iio_block_dequeue(entry->block, false)` —
  blocking.

So making capture interrupt-driven buys "the CPU sits in WFI while the hardware
sequences at its own rate" and **not** overlap between capture and protocol work.
That is still worth having, but it is not throughput.

The **client** (the PC) is a normal threaded libiio build; none of this applies
to it. Only the board is single-threaded, and it serves one command at a time:
while a block is being filled at the trigger rate, the board answers nothing
else.

If you add a WFI wait, guard the completion test with `__disable_irq()` around it
or the wakeup can arrive between test and sleep and be lost.

Also verified, and blocking for interrupt-driven ADC on the MAX32690:

1. **No ADC interrupt reaches no-OS.** `maxim_irq.c` defines ISRs for UART0-3,
   TMR0-2, DMA0-15, RTC and USB — there is no `ADC_IRQHandler` — and
   `enum no_os_irq_event` has no ADC event. Registering a callback for `ADC_IRQn`
   returns success and never fires. The port would have to define its own
   `ADC_IRQHandler`; the startup file's version is `.weak`, so that works.
2. **MSDK's async callback carries no instance pointer.** `adc_revb.c:49` is a
   single file-scope `static mxc_adc_complete_cb_t async_callback;` and `:300`
   invokes `(cb)(NULL, flags)`. `MXC_ADC_StartConversionAsync` also spins in
   `MXC_GetLock`. Write the ISR directly instead.
3. **ADC-DMA is unreachable.** `MXC_ADC_StartConversionDMA` needs
   `MXC_DMA_Handler` to run from `DMAn_IRQHandler`, but `maxim_irq.c:210-285`
   provides *strong* `DMA0..15_IRQHandler` that dispatch through no-OS's own
   action list and never call it.

## 11. Transports

One transport per image, selected by `WITH_IIOD_{UART,NETWORK,USB}`. Device
registration is done once, in `samples/iiod/main.c:noos_register_devices`, before
`noos_iiod_run()`; each transport then has the same shape:

```
iiod_init()
iio_create_context(&params, "no-os:")
iio_context_get_xml(ctx)           -> the context description sent to the host
loop { wait for a client; iiod_interpreter(ctx, pdata, read, write, xml, len) }
```

**Everything outside `no-OS/` is upstream `analogdevicesinc/libiio` main,
unmodified** — `tinyiiod/`, `iiod/responder.c`, `iiod-responder.c`. Earlier
work patched tinyiiod and the responder (a non-blocking step API, the v0 ASCII
protocol, multiple network clients); that is on branch
`backup/pre-upstream-reset-20260928`. The transports were rewritten to the
upstream contract instead:

- `iiod_interpreter()` **blocks** for a whole session and returns when a read
  or write fails. It handles the `"BINARY\r\n"` switch itself; only the v1
  binary protocol is served (`WITH_IIOD_V0_COMPAT=0`; `tinyiiod/ops-stubs.c`
  supplies the one v0 hook the responder links against).
- the read callback must **block until it has all `size` bytes**. Returning
  `-EAGAIN` ends the session, so no transport returns it; an idle or dead link
  is reported as `-ETIMEDOUT` / `-ENODEV`, which ends the session cleanly and
  the loop waits for the next client.

Transport specifics:

- **UART** (`iiod/uart.c`) — the read loops over `no_os_uart_read`, retrying on
  `-EAGAIN`. The interpreter never returns while the UART exists, so there is
  no session boundary. `noos_iiod_run_uart()` takes the UART as the caller's
  `no_os_uart_init_param`. `noos_iiod_run()` builds that from `UART_*` in the
  platform's `parameters.h` (`iiod/uart_params.c`, built when the server is
  given the platform's include dir).
- **Network** (`iiod/network.c`) — lwIP plus a netdev chip
  (ADIN1110 via no-OS Kconfig, `configs/network.conf`), static IP by default,
  port 30431. **One client at a time**: the accept loop steps lwIP until a
  connection arrives, runs the interpreter on it, then `net_client_close` +
  `net_drain`. The read steps lwIP while waiting and gives up after
  `NET_READ_TIMEOUT_S` (3 s) idle. Consequence: a client that opens a second
  socket (libiio's network backend does, for a buffer stream — `iio_rwdev`)
  has that socket accepted only after the first one idles out, i.e. about 3 s
  of latency before streaming starts. It works; it is not fast.
- **USB** (`iiod/usb.c`) — MAXUSB vendor interface with `IIO_USB_NUM_PIPES`
  (2) bulk pipe pairs, fed by a TMR0 tick that keeps an RX transfer queued per
  open pipe. **One interpreter serves both pipes**: a read with no command in
  progress waits on every open pipe and takes the first with data
  (`pipe_wait_any`), then stays locked on that pipe until the response is
  written, so a command's payload and reply never cross pipes. A session is
  "pipe 0 open with the `open_seq` it started with"; the host closing or
  reopening pipe 0, a bus reset or a disconnect ends it (`-ENODEV`), and
  `session_close` drops what every pipe still holds — except a pipe 0 the host
  has already reopened, which is the next session.
  A data pipe the host gives up on (Ctrl-C twice in `iio_rwdev`: the host
  cancels the pipe-1 transfer and sends the block/buffer teardown on pipe 0)
  is **stalled**, not fatal: a response blocked on it for `USB_STALL_TICKS`
  (200 ms) while pipe 0 has a command, or an RX error on it, drops the pipe
  (`pipe_stall`), its later responses fail with `-EPIPE`, and the session goes
  on over pipe 0. Reopening the pipe clears it. Without this the interpreter
  sat out the 10 s TX timeout, then ended the session on the pipe-1 error, and
  the host waited forever for its pipe-0 replies.

`iiod/responder.c` (1357 lines) is the shared v1 binary protocol implementation.
Opcode dispatch table at `:1217-1248`, covering attrs, buffers, blocks, event
streams, triggers and register access; `iiod_cmd` (`:1249-1263`) dispatches through
it. `buffer_enqueue_block` (`:293-320`) shifts enqueue errors left by 16 so the
client can tell an enqueue failure from a dequeue failure in one status word.

## 12. Value formatting

The table layer formats and parses the attributes with libiio's `iio_value_*`
helpers, declared in `<iio/iio-backend.h>` next to `iio_snprintf()`:

- `iio_value_format_int/str/fixed/list` write the value and return its length
  **including** the terminating NUL, as the responder expects, or `-ERANGE`
  if it does not fit;
- `iio_value_parse_int/fixed` accept one trailing newline;
- `iio_value_match_str` looks a written string up in a table.

They are integer and fixed-point only, so no float `printf` is needed. The text
forms match what a Linux IIO driver prints: plain integers, `int.micro` with
six fractional digits for `scale`, and bare strings for enumerations.

Every attribute read is capped at 256 bytes on this build:
`iiod/responder.c:24-26` sets `READ_ATTR_BUF_SIZE 256` under
`WITH_LIBTINYIIOD` (64 KiB otherwise), so keep values short.

## 13. Build system

The firmware is built **by upstream no-OS's own CMake + Kconfig**, the same way a
project under `no-OS/projects/` is. This port carries no toolchain files, board
files or MSDK source lists of its own: the compiler, cpu flags, startup code,
linker script, platform drivers, lwIP, the generated `no_os_config.h` and the
`flash`/`debug` targets all come from the no-OS checkout. `CMakeLists.txt` only
adds the libiio sources and links them against the `no-os` library target.

Nothing in the no-OS checkout is modified; it is only `add_subdirectory`'d.

### Presets

One preset per *transport*, one build dir each (`build-uart`, `build-usb`,
`build-network`, never the repo root). The board is an override, not a preset:

```
cmake --workflow --preset usb                     # configure + build, default board
cmake --build --preset flash-usb                  # flash it (also flash-uart, flash-network)
cmake --workflow --preset flash-usb               # configure + build + flash
cmake --preset usb --fresh -DBOARD=max32690fthr   # another board, same dir
cmake --build --preset usb
```

The default board is `ad-apard32690-sl` (hidden `iiod-base`, which also sets
Ninja, Debug, `compile_commands.json` and `PROBE=openocd`). The image lands in
`build-<transport>/build/libiio-noos.{elf,hex,bin}`.

**The board list is not copied here.** `cmake/no_os_board.cmake` looks `BOARD`
up in no-OS's own `board_configs/*/CMakePresets.json` and takes that preset's
`cacheVariables`, following `inherits`: `PLATFORM`, `TARGET`, `TARGET_NUM`,
`BOARD_CONFIG_FILE` and `USE_VENDOR_TOOLCHAIN`. A board no-OS adds is buildable
here the day the checkout has it. An unknown `BOARD` lists the known ones. A `-D`
for any of those variables still wins over the board's value.

CMake keeps the first configure's compiler and toolchain flags. So a dir that was
configured for one board refuses another, with the `--fresh` command to use,
instead of building the new board with the old one's flags.

Checked, all built:

| board | part | uart | usb | network |
|---|---|---|---|---|
| ad-apard32690-sl (default) | max32690 | x | x | x (ADIN1110) |
| max32690fthr | max32690 | x | x | |
| max32650fthr | max32650 | x | x | |
| ad-swiot1l-sl | max32650 | x | x | |
| max32666fthr | max32665 | x | x | |
| max32655fthr | max32655 | x | | |
| max32660fthr | max32660 | x | | |
| max32670evkit | max32670 | x | | |
| max78000fthr | max78000 | x | | |
| eval-adicup3029 | ADuCM3029 | x | | |

USB needs the USB-HS core (no-OS has a `maxim_usb_uart.c` for the part);
`samples/iiod/platform/maxim/platform.cmake` refuses it on the others with that reason.
Network assumes the ADIN1110 wiring of the AD-APARD32690-SL.
`samples/iiod/platform/aducm3029/platform.cmake` allows only uart. Only the AD-APARD32690-SL
has been run on hardware.

### Finding no-OS: `cmake/no_os_locate.cmake`

No path is hardcoded. It runs before `project()` (the toolchain file lives in
the checkout) and takes the first hit of:

1. `-DNO_OS_PATH=<dir>`
2. `$NO_OS_PATH`, `$NO_OS_DIR` or `$NOOS_DIR`
3. `<libiio>/../no-OS`, `~/no-OS`, `<libiio>/../*/no-OS`, `~/*/no-OS`
4. `-DNO_OS_FETCH=ON`: shallow clone of `NO_OS_GIT_TAG` (default `main`) into
   `build-<preset>/_deps/no-os`

A tree only counts if it has the CMake build (`board_configs/<PLATFORM>`,
`tools/scripts/generate_config.py`, `drivers/platform/<PLATFORM>/toolchain.cmake`),
so an old Make-only checkout is skipped. Several hits give a warning listing them;
the choice is cached as `NO_OS_PATH`.

### How the bridge works

- `CMAKE_TOOLCHAIN_FILE` is set to `${NO_OS_PATH}/drivers/platform/${PLATFORM}/toolchain.cmake`
  before `project()`; no-OS only picks one itself when it is the top-level project.
- The Kconfig input is the board defconfig from no-OS, then the project fragment.
  The fragment is `configs/<transport>.conf` (generic: UART, IRQ, GPIO, TIMER,
  DMA, plus SPI/NET/LWIP/ADIN1110/static IP for network) followed by
  `samples/iiod/platform/<PLATFORM>/configs/<transport>.conf` (e.g. `CONFIG_USB_UART_MAXIM`).
  They are merged into `build-*/iiod/<transport>.conf` and passed as
  `PROJECT_DEFCONFIG`, relative to this directory as no-OS expects.
  `boards/<transport>/<BOARD>.conf`, if one is added, overrides per board.
- `CONFIG_IIO` stays off: tinyiiod is the server, not no-OS's iio app.
- After `add_subdirectory(${NO_OS_PATH} no-os)`, `CMAKE_MODULE_PATH` gets the
  same dirs no-OS's own root `CMakeLists.txt` uses (`cmake`, `cmake/stm32`,
  `cmake/libraries`), plus `cmake/${PLATFORM}` when it exists. `project_utils`
  is included for `config_platform_sdk`, `add_flash_target` and
  `post_build_config`. `config_platform_sdk` is what honours no-OS's
  per-platform layout: it includes `cmake/<platform>/<platform>_platform_sdk.cmake`
  (aducm3029, pico, stm32, xilinx) and calls its `config_<platform>_sdk()`.
  Maxim has none, because its SDK is set up by the toolchain file.
- `samples/iiod/platform/<PLATFORM>/platform.cmake` sets `IIOD_PLATFORM_ADC`:
  `common_api` on max32690/max32655, `adc_demo` (no hardware) everywhere else.
  no-OS builds the MSDK ADC driver for both parts; for max32690 that needs
  no-OS with "drivers: platform: maxim: max32690: build the MSDK ADC driver",
  and configure stops on an older checkout. `_maxim_add_msdk()` still adds
  any MSDK source a target list leaves out, skipping those `no-os` already
  compiles, so there are no duplicate symbols.

Key variables:

| variable | default | meaning |
|---|---|---|
| `BOARD` | `ad-apard32690-sl` | any no-OS board; everything below it comes from its no-OS preset |
| `PLATFORM`, `BOARD_CONFIG_FILE`, `TARGET`, `TARGET_NUM` | from the board | no-OS platform, defconfig and part |
| `IIOD_TRANSPORT` | `uart` | `uart`, `usb` or `network` |
| `NO_OS_PATH` | discovered | the no-OS checkout |
| `NO_OS_FETCH`, `NO_OS_GIT_URL`, `NO_OS_GIT_TAG` | `OFF`, upstream, `main` | clone fallback |
| `ADC` | `IIOD_PLATFORM_ADC` | `samples/iiod/adc/<adc>/<adc>.c` |
| `PROBE` | `openocd` (preset) | no-OS flash tool; `jlink` also works |
| `MAXIM_LIBRARIES` (env) | from no-OS's maxim toolchain | MSDK `Libraries/` dir |

### Other platforms

The per-platform glue is the sample firmware's board wiring, like a no-OS
project's `src/platform/<platform>/` and `pinmux_config.c`, so it lives under
`samples/iiod/platform/`. The no-OS module (`module.cmake`) uses none of it:
`iio_app` gives the server its UART at run time.

The build is not Maxim-only: the board picks the no-OS platform. A platform
without `samples/iiod/platform/<PLATFORM>/parameters.h` fails at configure time with a message
that the iiod glue is missing (UART id, trigger timer, IRQ controller: see
`samples/iiod/platform/maxim/`). Adding one takes that glue and nothing else: the board already
resolves (`-DBOARD=nucleo-f756zg` gets `PLATFORM=stm32` from no-OS and stops
only at the missing glue). The no-OS side and its SDK install are unchanged.

A platform that needs board bring-up before any driver (clocks, pin mux)
defines `noos_platform_init()` in its glue. The sample's `main` calls it first,
and its weak default does nothing.

`samples/iiod/platform/aducm3029/` (EVAL-ADICUP3029) is the second one:

```
export CCES_HOME=/opt/analog/cces/3.0.3
cmake --preset uart -B build-uart-adicup3029 -DBOARD=eval-adicup3029
cmake --build build-uart-adicup3029 [--target flash]
```

- `noos_platform_init()` (`platform.c`) runs no-OS's `platform_init()`: power,
  HCLK/PCLK at 26 MHz (the UART baud table needs it) and the pin mux. Maxim
  needs none.
- the pin mux is `pinmux_config.c` (UART0 only), passed to CCES through
  `CONFIG_ADUCM_PINMUX_PATH` in `configs/uart.conf`. no-OS resolves a relative
  path against `projects/<name>/`, so the fragment gets the absolute path by
  `@IIOD_PLATFORM_DIR@` substitution.
- the trigger tick is no-OS timer id 1 (GP1; no-OS keeps GP0 for delays) on
  PCLK / 64 = 406250 Hz, through the generic `iio_trigger_timer_no_os.c`.
  `sampling_frequency` runs from about 6.2 Hz, the 16-bit counter's limit, up
  to 100 kHz. On this platform the callback handle is the timer descriptor,
  and the priority goes to `TMR1_EVT_IRQn`. This needs no-OS with "drivers:
  platform: aducm3029: fix timer and IRQ callback handling". Before that fix,
  the unregister passed the timer to `adi_uart_RegisterCallback`, and a timer
  initialized again after its first register never called back, so the
  trigger stopped after the first buffer.
- the ADC is `adc_demo` (`adc-demo`, no hardware).
- `WITH_IIOD_V0_COMPAT` defaults to OFF. The v0 flex scanner allocates a
  16 KiB buffer per session, half the 32 KiB heap, and `exit()`s when it can't.
- the image is about 124 KB of flash (165 KB with v0), and `.data` + `.bss`
  is about 6.5 KiB. no-OS's 8 KiB stack and 32 KiB heap (DSRAM_B) are kept.

Without `BOARD` the directory is a host project that builds only the PC-side
API tests (`tests/`).

The server half is `libiio_noos_add_server()` in
`cmake/libiio_noos_server.cmake`: an OBJECT library with the libiio core,
tinyiiod, the responder, `backend.c` and `iiod/<transport>.c`. Its defines turn
off every upstream backend (`WITH_LOCAL_BACKEND=0` and friends). They also set
`WITH_LIBTINYIIOD=1`, `WITH_EXTERNAL_BACKEND=1` and `NO_THREADS=1`, and pin the
version strings. They are PUBLIC, together with `include/` and `no-OS/include`,
so the drivers linked next to it see the same `<iio/iio.h>`. The rest
of libiio's headers stay private. It takes `no-os` as `$<COMPILE_ONLY:...>`:
headers, defines and the forced `no_os_config.h`, but not the objects, which
reach the link through the executable once. `CMakeLists.txt` here is one
caller.

Gotchas:

- the MSDK startup file sizes the main stack from `__STACK_SIZE`, default
  4 KiB, which the responder overflows as soon as a buffer is enabled.
  `samples/iiod/platform/maxim/platform.cmake` defines it on the `no-os` target (where the
  startup file is compiled) from `IIOD_STACK_SIZE`, default 64 KiB — as no-OS
  does itself for aducm3029.
- no-OS's Maxim include path uses the MSDK's old CMSIS 4 `core_cm4.h`, which has
  no `NVIC_GetEnableIRQ`. `iio_usb_backend.c` reads `NVIC->ISER` instead.
- `CONFIG_ADIN1110` is inside `menuconfig NET`, so `CONFIG_NET=y` is required.
- `post_build_config` writes `.vscode/` launch configs into this directory.
- under WSL2, `usbip` must be re-attached after every flash.
- no-OS `maxim_irq.c` keeps every callback in one list with a **single shared
  iterator** (`no_os_list_read_find`), which each timer ISR walks. Two things
  follow:
  - A register or unregister done in thread context and interrupted by that
    walk acts on whatever node the ISR left the iterator on.
  - `_timer_common_callback` returns without `MXC_TMR_ClearFlags` when it finds
    no callback, so the timer's interrupt then fires forever.

  Change the list with interrupts off, and give all timer ISRs the same
  priority. Also, `no_os_irq_ctrl_remove` frees the callbacks of **every**
  peripheral, so never call it on the shared controller.

### libiio as a no-OS module

A no-OS project can serve its devices with libiio's IIOD, for libiio v1
clients, by enabling libiio as a no-OS library module. no-OS's own IIO server
(`iio/`, `CONFIG_IIO`, `iio_app`) is then not used. Devices are described with
this port's device API (`struct noos_iio_device_info`, `include/iio_device.h`),
in no-OS drivers or in the project.

```
export NO_OS_DEP_LIBIIO_PATH=<this libiio checkout>
cd <no-OS>
python tools/scripts/no_os_build.py build --project eval-adxl355-pmdz \
       --variant iio --board eval-adicup3029
```

The pieces:

- **hook in no-OS:** `cmake/libraries/libiio.cmake`, included by no-OS's
  `libraries/` when `CONFIG_LIBIIO` is set. It finds this tree like any no-OS
  library (the `NO_OS_DEP_LIBIIO_PATH` override, else a clone of
  `CONFIG_LIBIIO_VERSION`) and includes `module.cmake`.
- **Kconfig:** `CONFIG_LIBIIO` and `CONFIG_LIBIIO_VERSION` live in no-OS's
  `libraries/Kconfig`. no-OS runs Kconfig before CMake fetches any library, so the
  switch has to be in its own tree, as lwip's and mqtt's are.
- **`module.cmake`:**
  - builds `libiio_noos_add_server(libiio-iiod TRANSPORT uart)` (the libiio
    core, tinyiiod, the responder, `backend.c`, `iiod/uart.c`);
  - links it into `${NO_OS_PROJECT_NAME}`. It's included from `libraries/`,
    after `projects/`, so the executable already exists;
  - puts the device API (`include/`, libiio's `include/`, `LIBIIO_STATIC`) on
    the `no-os` target, so no-OS drivers written against it build there.
- **registration:** at run time, `noos_iio_register_device()`, because drivers
  allocate their state; then `noos_iiod_run_uart()` with the project's own
  `no_os_uart_init_param`. The server needs no `parameters.h`.

The first driver is no-OS's `drivers/accel/adxl355/iio_adxl355.c`
(`CONFIG_ACCEL_IIO_ADXL355`, which depends on `CONFIG_LIBIIO`):

- `adxl355_iio_init()` brings the part up in measurement mode, and
  `adxl355_iio_get_device_info()` fills the `noos_iio_device_info`;
- it has the attributes of no-OS's earlier IIO driver;
- buffered scans are paced by the part's `DATA_RDY`;
- eval-adxl355-pmdz's `iio_example.c` registers it and runs the server, for
  the `iio`, `iio_adxl357` and `iio_adxl359` variants.

The project's trigger and LWIP examples need no-OS's own server; configuring
them stops with an error that says so. `iio_adxl355_trig.c` is kept in the tree
but not built. Keep `CONFIG_IIO` off, because its `iio/iiod.c` also defines
`iiod_init`, and print nothing on the IIOD UART.

On the ADuCM3029 the eval-adxl355-pmdz image is about 99 KB of flash and
6.4 KiB of `.data` + `.bss`, with no-OS's 8 KiB stack and 32 KiB heap. The
client sets the block size, so keep `iio_rwdev -b` small.
