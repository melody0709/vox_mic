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
import java.util.Locale;

public class RecordThread extends Thread {
    private static final String SOCKET_NAME = "voxmicsource";

    private final RecordService service;
    private final AudioRecord recorder;
    private volatile LocalServerSocket serverSocket;
    private long mBlockCount = 0;
    private long mDiagSamples = 0;
    private double mDiagSquareSum = 0.0;
    private long mDiagZeroSamples = 0;
    private int mDiagPeak = 0;

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
                resetAudioDiagnostics();
                while (!Thread.currentThread().isInterrupted()) {
                    int totalRead = 0;
                    while (totalRead < BLOCK_SIZE) {
                        int r = recorder.read(buf, totalRead, BLOCK_SIZE - totalRead);
                        if (r < 0) break;
                        totalRead += r;
                    }
                    if (totalRead <= 0) break;
                    accumulateAudioDiagnostics(buf, totalRead);
                    socket.getOutputStream().write(buf, 0, totalRead);

                    mBlockCount++;
                    if (mBlockCount % 100 == 0) {
                        logAudioDiagnostics("periodic");
                    }
                }
            } catch (IOException e) {
                Log.e(App.TAG, "LocalSocket", e);
            } finally {
                try { recorder.stop(); } catch (Exception ignored) {}
                logAudioDiagnostics("final");
                Log.i(App.TAG, "connection closed, blocks sent=" + mBlockCount);
            }
        }

        try {
            if (serverSocket != null) serverSocket.close();
        } catch (IOException e) {
            Log.e(App.TAG, "LocalServerSocket (close)", e);
        }
    }

    private void resetAudioDiagnostics() {
        mDiagSamples = 0;
        mDiagSquareSum = 0.0;
        mDiagZeroSamples = 0;
        mDiagPeak = 0;
    }

    private void accumulateAudioDiagnostics(byte[] data, int length) {
        int alignedLength = length & ~1;
        for (int i = 0; i < alignedLength; i += 2) {
            int sample = (short) ((data[i] & 0xff) | (data[i + 1] << 8));
            int magnitude = Math.abs(sample);
            if (magnitude > mDiagPeak) mDiagPeak = magnitude;
            if (sample == 0) mDiagZeroSamples++;
            mDiagSquareSum += (double) sample * (double) sample;
            mDiagSamples++;
        }
    }

    private void logAudioDiagnostics(String reason) {
        if (mDiagSamples == 0) return;
        double rms = Math.sqrt(mDiagSquareSum / (double) mDiagSamples);
        double rmsDbfs = rms > 0.0 ? 20.0 * Math.log10(rms / 32768.0) : -120.0;
        double peakDbfs = mDiagPeak > 0
                ? 20.0 * Math.log10((double) mDiagPeak / 32768.0) : -120.0;
        double zeroRatio = (double) mDiagZeroSamples / (double) mDiagSamples;
        Log.i(App.TAG, String.format(Locale.US,
                "audio %s blocks=%d rms=%.1fdBFS peak=%.1fdBFS zero=%.4f",
                reason, mBlockCount, rmsDbfs, peakDbfs, zeroRatio));
        resetAudioDiagnostics();
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
