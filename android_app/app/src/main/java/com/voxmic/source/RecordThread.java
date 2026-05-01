package com.voxmic.source;

import android.media.AudioRecord;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Build;
import android.os.SystemClock;
import android.system.Os;
import android.util.Log;

import java.io.IOException;

public class RecordThread extends Thread {
    private static final int SAMPLE_RATE = 48000;
    private static final int CHANNELS = 1;
    private static final int BYTES_PER_FRAME = 2 * CHANNELS;
    private static final String SOCKET_NAME = "voxmicsource";

    private final RecordService service;
    private final AudioRecord recorder;
    private volatile LocalServerSocket serverSocket;
    private long mBlockCount = 0;

    RecordThread(RecordService service, AudioRecord recorder) {
        this.service = service;
        this.recorder = recorder;
    }

    @Override
    public void run() {
        try {
            serverSocket = new LocalServerSocket(SOCKET_NAME);
        } catch (IOException e) {
            Log.e(App.TAG, "LocalServerSocket (bind)", e);
        }

        // Align: 480 frames = 960 bytes = 10.0ms at 48000Hz
        final int BLOCK_SIZE = 960;
        byte[] buf = new byte[BLOCK_SIZE];

        while (!Thread.currentThread().isInterrupted()) {
            service.showNotificationListening();

            try (LocalSocket socket = serverSocket.accept()) {
                if (Thread.currentThread().isInterrupted()) break;

                service.showNotificationEstablished();
                recorder.startRecording();

                while (!Thread.currentThread().isInterrupted()) {
                    long t1 = SystemClock.elapsedRealtimeNanos();
                    int totalRead = 0;
                    while (totalRead < BLOCK_SIZE) {
                        int r = recorder.read(buf, totalRead, BLOCK_SIZE - totalRead);
                        if (r < 0) break;
                        totalRead += r;
                    }
                    long t2 = SystemClock.elapsedRealtimeNanos();
                    if (totalRead <= 0) break;
                    socket.getOutputStream().write(buf, 0, totalRead);

                    if (mBlockCount++ % 100 == 0) {
                        float readMs = (float)(t2 - t1) * 1e-6f;
                        Log.i(App.TAG, String.format("[Latency] read=%.1fms",
                            readMs));
                    }
                }
            } catch (IOException e) {
                Log.e(App.TAG, "LocalSocket", e);
            } finally {
                recorder.stop();
            }
        }

        try {
            serverSocket.close();
        } catch (IOException e) {
            Log.e(App.TAG, "LocalServerSocket (close)", e);
        }
    }

    public void interrupt() {
        super.interrupt();
        if (Build.VERSION.SDK_INT >= 21) {
            try {
                Os.shutdown(serverSocket.getFileDescriptor(), 0);
            } catch (Exception e) {
                Log.e(App.TAG, "os.shutdown", e);
            }
        } else {
            try (LocalSocket socket = new LocalSocket()) {
                socket.connect(new LocalSocketAddress(SOCKET_NAME));
            } catch (IOException e) {
                Log.e(App.TAG, "LocalSocket (connect back)", e);
            }
        }
    }
}
