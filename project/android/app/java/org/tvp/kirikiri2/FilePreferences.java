package org.tvp.kirikiri2;

import android.content.SharedPreferences;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;
import java.util.WeakHashMap;

/**
 * SharedPreferences kept in one JSON file of a folder the core may write, for a launch by
 * Enginehost. The platform's own implementation only writes under the app's data directory
 * (Context.getSharedPreferences(File, int) is not public API), and under Enginehost's lockdown
 * that directory is not the core's (Droidtop/tracker#550). Each value is stored with its type, so
 * a getInt reads back an int as the platform's would.
 */
final class FilePreferences implements SharedPreferences {
    private static final String TAG = "kr2activaty";
    private static final Map<String, FilePreferences> OPEN = new HashMap<>();

    private final File file;
    private final Map<String, Object> values = new HashMap<>();
    private final Set<OnSharedPreferenceChangeListener> listeners =
            java.util.Collections.newSetFromMap(new WeakHashMap<OnSharedPreferenceChangeListener, Boolean>());

    /** One instance per file, as the platform keeps one per name. */
    static synchronized FilePreferences open(File file) {
        FilePreferences prefs = OPEN.get(file.getAbsolutePath());
        if (prefs == null) {
            prefs = new FilePreferences(file);
            OPEN.put(file.getAbsolutePath(), prefs);
        }
        return prefs;
    }

    private FilePreferences(File file) {
        this.file = file;
        if (!file.isFile()) return;
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] bytes = new byte[(int) file.length()];
            int read = 0;
            while (read < bytes.length) {
                int n = in.read(bytes, read, bytes.length - read);
                if (n < 0) break;
                read += n;
            }
            JSONObject json = new JSONObject(new String(bytes, 0, read, StandardCharsets.UTF_8));
            for (Iterator<String> keys = json.keys(); keys.hasNext(); ) {
                String key = keys.next();
                Object value = decode(json.getJSONObject(key));
                if (value != null) values.put(key, value);
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not read preferences " + file, e);
        }
    }

    private static Object decode(JSONObject entry) throws org.json.JSONException {
        switch (entry.getString("t")) {
            case "s": return entry.getString("v");
            case "i": return entry.getInt("v");
            case "l": return entry.getLong("v");
            case "f": return (float) entry.getDouble("v");
            case "b": return entry.getBoolean("v");
            case "set": {
                JSONArray array = entry.getJSONArray("v");
                Set<String> set = new HashSet<>();
                for (int i = 0; i < array.length(); i++) set.add(array.getString(i));
                return set;
            }
            default: return null;
        }
    }

    private static JSONObject encode(Object value) throws org.json.JSONException {
        JSONObject entry = new JSONObject();
        if (value instanceof String) entry.put("t", "s").put("v", value);
        else if (value instanceof Integer) entry.put("t", "i").put("v", value);
        else if (value instanceof Long) entry.put("t", "l").put("v", value);
        else if (value instanceof Float) entry.put("t", "f").put("v", (double) (Float) value);
        else if (value instanceof Boolean) entry.put("t", "b").put("v", value);
        else if (value instanceof Set) entry.put("t", "set").put("v", new JSONArray((Set<?>) value));
        return entry;
    }

    /** Writes the whole file beside itself, then renames it over the old one. */
    private boolean write() {
        try {
            JSONObject json = new JSONObject();
            for (Map.Entry<String, Object> e : values.entrySet()) json.put(e.getKey(), encode(e.getValue()));
            File parent = file.getParentFile();
            if (parent != null) parent.mkdirs();
            File partial = new File(file.getPath() + ".partial");
            try (FileOutputStream out = new FileOutputStream(partial)) {
                out.write(json.toString().getBytes(StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            return partial.renameTo(file);
        } catch (Exception e) {
            Log.w(TAG, "Could not write preferences " + file, e);
            return false;
        }
    }

    private synchronized Object get(String key) {
        return values.get(key);
    }

    @Override public synchronized Map<String, ?> getAll() { return new HashMap<>(values); }

    @Override public String getString(String key, String defValue) {
        Object v = get(key);
        return v instanceof String ? (String) v : defValue;
    }

    @SuppressWarnings("unchecked")
    @Override public Set<String> getStringSet(String key, Set<String> defValues) {
        Object v = get(key);
        return v instanceof Set ? new HashSet<>((Set<String>) v) : defValues;
    }

    @Override public int getInt(String key, int defValue) {
        Object v = get(key);
        return v instanceof Integer ? (Integer) v : defValue;
    }

    @Override public long getLong(String key, long defValue) {
        Object v = get(key);
        return v instanceof Long ? (Long) v : defValue;
    }

    @Override public float getFloat(String key, float defValue) {
        Object v = get(key);
        return v instanceof Float ? (Float) v : defValue;
    }

    @Override public boolean getBoolean(String key, boolean defValue) {
        Object v = get(key);
        return v instanceof Boolean ? (Boolean) v : defValue;
    }

    @Override public synchronized boolean contains(String key) { return values.containsKey(key); }

    @Override public Editor edit() { return new FileEditor(); }

    @Override public synchronized void registerOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {
        listeners.add(listener);
    }

    @Override public synchronized void unregisterOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {
        listeners.remove(listener);
    }

    private final class FileEditor implements Editor {
        private final Map<String, Object> puts = new HashMap<>();
        private final Set<String> removes = new HashSet<>();
        private boolean clear;

        @Override public Editor putString(String key, String value) { return put(key, value); }
        @Override public Editor putStringSet(String key, Set<String> values) {
            return put(key, values == null ? null : new HashSet<>(values));
        }
        @Override public Editor putInt(String key, int value) { return put(key, value); }
        @Override public Editor putLong(String key, long value) { return put(key, value); }
        @Override public Editor putFloat(String key, float value) { return put(key, value); }
        @Override public Editor putBoolean(String key, boolean value) { return put(key, value); }

        private synchronized Editor put(String key, Object value) {
            // A null value removes the key, as the platform's editor does.
            if (value == null) {
                removes.add(key);
                puts.remove(key);
            } else {
                puts.put(key, value);
                removes.remove(key);
            }
            return this;
        }

        @Override public synchronized Editor remove(String key) {
            removes.add(key);
            puts.remove(key);
            return this;
        }

        @Override public synchronized Editor clear() {
            clear = true;
            return this;
        }

        @Override public boolean commit() {
            Set<String> changed = new HashSet<>();
            Set<OnSharedPreferenceChangeListener> notify;
            boolean written;
            synchronized (FilePreferences.this) {
                synchronized (this) {
                    if (clear) {
                        changed.addAll(values.keySet());
                        values.clear();
                    }
                    for (String key : removes) {
                        if (values.remove(key) != null) changed.add(key);
                    }
                    values.putAll(puts);
                    changed.addAll(puts.keySet());
                }
                written = write();
                notify = new HashSet<>(listeners);
            }
            for (OnSharedPreferenceChangeListener listener : notify) {
                for (String key : changed) listener.onSharedPreferenceChanged(FilePreferences.this, key);
            }
            return written;
        }

        @Override public void apply() { commit(); }
    }
}
