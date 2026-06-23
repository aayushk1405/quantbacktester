// frontend/src/components/FileUpload.tsx
// Reusable drag-and-drop file upload component.
// Accepts a file via drag-and-drop or click-to-browse,
// validates it client-side, then calls onFile with the File object.

import { useRef, useState } from "react";

interface Props {
  accept?: string;          // e.g. ".csv"
  maxSizeMb?: number;
  onFile: (file: File) => void;
  disabled?: boolean;
}

type DropState = "idle" | "hover" | "error";

export default function FileUpload({
  accept = ".csv",
  maxSizeMb = 50,
  onFile,
  disabled = false,
}: Props) {
  const [dropState, setDropState] = useState<DropState>("idle");
  const [errorMsg, setErrorMsg]   = useState("");
  const [fileName, setFileName]   = useState("");
  const inputRef = useRef<HTMLInputElement>(null);

  const validate = (file: File): string | null => {
    if (accept && !file.name.endsWith(accept))
      return `Only ${accept} files are accepted`;
    if (file.size > maxSizeMb * 1024 * 1024)
      return `File exceeds ${maxSizeMb} MB limit`;
    return null;
  };

  const handleFile = (file: File) => {
    const err = validate(file);
    if (err) {
      setDropState("error");
      setErrorMsg(err);
      setFileName("");
      return;
    }
    setDropState("idle");
    setErrorMsg("");
    setFileName(file.name);
    onFile(file);
  };

  const onDragOver  = (e: React.DragEvent) => { e.preventDefault(); if (!disabled) setDropState("hover"); };
  const onDragLeave = ()                    => setDropState("idle");
  const onDrop      = (e: React.DragEvent) => {
    e.preventDefault();
    if (disabled) return;
    const file = e.dataTransfer.files[0];
    if (file) handleFile(file);
    else setDropState("idle");
  };
  const onChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) handleFile(file);
  };

  return (
    <div
      className={`dropzone ${dropState} ${disabled ? "disabled" : ""}`}
      onDragOver={onDragOver}
      onDragLeave={onDragLeave}
      onDrop={onDrop}
      onClick={() => !disabled && inputRef.current?.click()}
      role="button"
      aria-label="Upload file"
      tabIndex={0}
      onKeyDown={e => e.key === "Enter" && inputRef.current?.click()}
    >
      <input
        ref={inputRef}
        type="file"
        accept={accept}
        style={{ display: "none" }}
        onChange={onChange}
      />

      {fileName ? (
        <div className="dropzone-success">
          <i className="ti ti-circle-check" />
          <span>{fileName}</span>
        </div>
      ) : (
        <div className="dropzone-prompt">
          <i className="ti ti-file-upload" />
          <p className="dropzone-main">
            {dropState === "hover" ? "Drop it!" : "Drag & drop a CSV file here"}
          </p>
          <p className="dropzone-sub">or click to browse — max {maxSizeMb} MB</p>
        </div>
      )}

      {dropState === "error" && (
        <p className="dropzone-error">{errorMsg}</p>
      )}
    </div>
  );
}
