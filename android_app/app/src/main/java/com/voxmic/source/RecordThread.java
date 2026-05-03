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
            return;
        }

        final int BLOCK_SIZE = 960;
        byte[] buf = new byte[BLOCK_SIZE];

        while (!Thread.currentThread().isInterrupted()) {
            service.showNotificationListening();

            try (LocalSocket socket = serverSocket.accept()) {
                if (Thread.currentThread().isInterrupted()) break;
                Log.i(App.TAG, "accept() returned, client connected");

                service.showNotificationEstablished();
                recorder.startRecording();
                Log.i(App.TAG, "startRecording() OK, state=" + recorder.getState());

                mBlockCount = 0;
                while (!Thread.currentThread().isInterrupted()) {
                    int totalRead = 0;
                    while (totalRead < BLOCK_SIZE) {
                        int r = recorder.read(buf, totalRead, BLOCK_SIZE - totalRead);
                        if (r < 0) break;
                        totalRead += r;
                    }
                    if (totalRead <= 0) break;
                    socket.getOutputStream().write(buf, 0, totalRead);

                    if (mBlockCount++ % 100 == 0) {
                        Log.i(App.TAG, "blocks sent=" + mBlockCount);
                    }
                }
            } catch (IOException e) {
                Log.e(App.TAG, "LocalSocket", e);
            } finally {
                try { recorder.stop(); } catch (Exception ignored) {}
                Log.i(App.TAG, "connection closed, blocks sent=" + mBlockCount);
            }
        }

        try {
            if (serverSocket != null) serverSocket.close();
        } catch (IOException e) {
            Log.e(App.TAG, "LocalServerSocket (close)", e);
        }
    }

    public void interrupt() {
        super.interrupt();
        if (serverSocket == null) return;
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
