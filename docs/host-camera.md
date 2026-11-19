# Camera cartridges

In Qt, open **Settings** and choose **Camera input**. **System Camera** uses
the operating system's default camera. **Generated Pattern** uses the core's
deterministic sensor and guest-clock exposure timing. Choosing a source
applies it to the current window and sets the default for new windows.
Changing input cancels a capture in progress.

Use `--camera-source generated` or `--camera-source system` to select input
for one launch, including any new windows opened from that session.

In System Camera mode, the first shutter request opens the device and starts
an asynchronous, in-memory capture. The frontend opens no preview window and
creates no image file.

If camera permission is denied, no device is available, or capture fails, the
cartridge keeps its last accepted image and its busy bit clears. Before the first
successful capture, that image is black. A request also finishes this way if no
image arrives within ten seconds. CupidGB reports a changed error in the status
bar without interrupting the game with a dialog. The next capture can retry the
device. Closing the cartridge releases the camera session.

## Images and timing

A successful capture supplies one 128 by 112 grayscale sensor image. Common
eight-bit planar and semi-planar YUV formats, YUYV, UYVY and Y8 retain the delivered
luminance values. For example, video-range Y values 16 and 235 remain 16 and 235;
they are not expanded to 0 and 255. Mapped RGB uses Qt's image-format conversion
and `qGray`. Other supported packed RGB and YUV layouts are copied into a frame
with neutral orientation before Qt decodes their colors; color space, range and
transfer metadata stay intact. These color conversions can have different levels
from a directly sampled Y plane.

JPEG camera buffers use Qt's JPEG decoder with format detection disabled. Their
mapped byte span is checked independently of the row stride, which compressed
camera buffers may leave unset. Encoded data is limited to 64 MiB, and the decoded
dimensions must match the frame metadata, stay within 16,384 pixels per side and
contain at most 67,108,864 pixels. Embedded EXIF orientation is ignored so that
the common frame-orientation step applies once. Unsupported compressed formats,
unmappable buffers and invalid image dimensions fail the capture and retain the
previous image. Raw subsampled formats also require complete chroma rows and
columns before mapping; tiny or odd 4:2:0 buffers and odd-width 4:2:2 buffers are
rejected.

Conversion respects scan direction and Qt's frame orientation. Qt 6.8 and later
apply surface and presentation orientation separately; older releases use their
effective frame rotation and mirror flags. Qt's smooth scaling covers 130 by 114
pixels before taking the centered 128 by 112 crop. This defines CupidGB's host
conversion policy.
Equal crop dimensions do not establish identical interpolation or camera-driver
processing across platforms.

Conversion runs once per captured image. Ordinary cartridge reads use the
retained sensor bytes through the emulated gain, exposure, edge and dither
processing. Register writes can therefore change how those bytes become tiles.
The cartridge's busy bit blocks camera SRAM until the host completes the
request. A second shutter write can change its mode bits but does not cancel an
outstanding capture.

The generated sensor works in the Qt frontend and in applications that embed
the core without a camera provider. It runs without opening or enumerating
camera devices.

## Save states, reset and rewind

Camera images and pending device requests belong to the receiving session. They
are not stored in native state files. Saving during a host capture writes an idle
camera into the existing native camera section while leaving the live capture
untouched. The file's busy-bit/countdown consistency checks remain strict.

After a complete successful load into a session with a camera provider, the
saved busy bit and countdown clear. This also applies when the file came from a
pending generated capture. The receiving image stays current. A host-origin
file loaded without a provider starts idle and uses the generated sensor.
Ordinary generated captures still resume their saved countdown when the
receiver has no provider.

Reset, Quick Reset and Reload retain the receiving image and cancel obsolete
requests only when their cartridge transaction succeeds. Opening a cartridge
starts with a black image. Failed reset, cartridge and state loads preserve the
active request. Request tokens survive registration changes and successful
timeline operations; an old completion cannot replace pixels belonging to a
newer request. Tokens are never reused after counter exhaustion.

Rewind and debugger restoration use the same native-state rule. They retain the
current host image, rather than reconstructing earlier webcam frames. Replayed
guest instructions may request another capture; each later timeline restore
invalidates work from the abandoned timeline. This is live external input, so
host-camera replay is not a deterministic reconstruction of a previous scene.

Successful debugger Backstep, memory writes, register edits and memory-search
edits also cancel queued or submitted captures. Cancellation clears the pending
request and cartridge busy bit while retaining the accepted image. Rejected
edits preserve the active request. A later shutter request can start a fresh
capture; completion from the abandoned timeline cannot replace its pixels.

## Embedding API

`cupid_gb_set_camera_callback` registers a request callback and its context. The
same pair is idempotent; a changed pair cancels pending work and clears the old
image. A null callback selects the generated sensor. The callback may finish a
capture immediately, but it must not step, reset or destroy the machine.

For asynchronous work, queue the request and return from the guest callback.
Call `cupid_gb_camera_complete` at a core boundary on the owning thread, using
the request token and exactly 14,336 tightly packed grayscale bytes. Pass NULL
and zero bytes to finish a failed capture without replacing the image. Invalid
payloads and stale tokens leave the camera unchanged. An embedding application
must also reject results for a destroyed or replaced machine instance.

## Validation scope

The isolated Windows build passed nine focused checks covering host request
lifetime, all four native load APIs, strict malformed-state rejection,
generated/host state exchange, reset/load failures and injected Qt workflows.
The expanded converter check then passed 512 generated orientation cases on
Windows with Qt 6.8.3 and 64 on Linux with Qt 6.4.2, including JPEG frames with
EXIF orientation. Malformed payload and raw-frame geometry checks also passed.
These results retain their isolated build scope. The existing generated-sensor
fixtures remain unchanged.

The [combined version 20 integration](validation-storage-wave.md) passes the
Windows and Linux Qt build gates and native workflows on both hosts. Each native
camera record covers 28 requests, 62 cancellations, thirteen ignored stale
results and eight debugger or memory edit timeline cases. The checks cover queued
guest dispatch, save purity, image retention across reset/reload/rewind,
rejection of old owners and preservation of requests after failed transactions
or tool edits. Both runs use injected images and do not open a physical camera.
All thirty-two retained native workflow screenshots were reviewed without an
identified layout, clipping or branding defect in those captures.

Physical camera capture, permission UI, device removal/reconnection and sustained
Camera cartridge gameplay require separate Windows and Linux runtime evidence.
Generated-frame and injected-source tests do not qualify those workflows or
physical sensor accuracy.
