;;; aoxim-mode.el --- Major mode for aoxim math language -*- lexical-binding: t; -*-

;; Copyright (C) 2025
;; Author: xsoder
;; Keywords: languages, math, aoxim
;; Version: 0.2
;; Package-Requires: ((emacs "25.3"))

;;; Commentary:

;; Major mode for editing and running aoxim math language files.
;; Provides syntax highlighting, indentation, and an inferior REPL mode
;; similar to Haskell mode.
;;
;; Usage:
;;   M-x aoxim-mode         - Enable major mode for .aoxim files
;;   M-x run-aoxim          - Start interactive REPL
;;   C-c C-l                - Load current file into REPL
;;   C-c C-r                - Send region to REPL
;;   C-c C-c                - Send current definition to REPL
;;   M-x compile            - Compile with error navigation

(require 'comint)
(require 'compile)

;;; ============================================================================
;;; Customization
;;; ============================================================================

(defgroup aoxim nil
  "Major mode for aoxim language."
  :group 'languages
  :prefix "aoxim-")

(defcustom aoxim-program-name "aoxim"
  "Program invoked by the `run-aoxim' command."
  :type 'string
  :group 'aoxim)

(defcustom aoxim-use-colors t
  "Whether to use --color flag when running aoxim."
  :type 'boolean
  :group 'aoxim)

(defcustom aoxim-prompt-regexp "^λ "
  "Regexp to match aoxim REPL prompt."
  :type 'regexp
  :group 'aoxim)

;;; ============================================================================
;;; Syntax Table
;;; ============================================================================

(defvar aoxim-mode-syntax-table
  (let ((st (make-syntax-table)))
    ;; Comments: # to end of line
    (modify-syntax-entry ?# "<" st)
    (modify-syntax-entry ?\n ">" st)
    ;; Strings
    (modify-syntax-entry ?\" "\"" st)
    (modify-syntax-entry ?\' "\"" st)
    ;; Operators
    (modify-syntax-entry ?_ "w" st)
    (modify-syntax-entry ?+ "." st)
    (modify-syntax-entry ?- "." st)
    (modify-syntax-entry ?* "." st)
    (modify-syntax-entry ?/ "." st)
    (modify-syntax-entry ?% "." st)
    (modify-syntax-entry ?< "." st)
    (modify-syntax-entry ?> "." st)
    (modify-syntax-entry ?= "." st)
    (modify-syntax-entry ?! "." st)
    st)
  "Syntax table for `aoxim-mode'.")

;;; ============================================================================
;;; Keywords and Font-lock
;;; ============================================================================

(defconst aoxim-keywords
  '("if" "else" "while" "lambda" "const" "import"
    "return" "break" "continue" "True" "False" "struct" "self" "@"))

(defconst aoxim-builtins
  '("print" "len" "range" "help" "type" "assert" "test" "link" "extern" "match" "os"))

(defconst aoxim-constants
  '("None"))

(defvar aoxim-font-lock-keywords
  `(
    (,(regexp-opt aoxim-keywords 'symbols) . font-lock-keyword-face)
    (,(regexp-opt aoxim-builtins 'symbols) . font-lock-builtin-face)
    (,(regexp-opt aoxim-constants 'symbols) . font-lock-constant-face)
    ("\\<\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*("
     (1 font-lock-function-name-face))
    ("^\\s-*\\<\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*="
     (1 font-lock-variable-name-face))
    ("\\<[0-9]+\\(\\.[0-9]+\\)?\\>" . font-lock-constant-face)
    ("lambda\\s-+\\([A-Za-z_][A-Za-z0-9_]*\\)"
     (1 font-lock-variable-name-face))
    )
  "Syntax highlighting for `aoxim-mode'.")

;;; ============================================================================
;;; Indentation
;;; ============================================================================

(defun aoxim-indent-line ()
  "Indent current line for aoxim."
  (interactive)
  (let ((indent-col 0)
        (pos (- (point-max) (point))))
    (save-excursion
      (beginning-of-line)
      ;; De-indent else
      (when (looking-at "^[ \t]*else\\b")
        (setq indent-col (- indent-col tab-width)))
      (when (looking-at "^[ \t]*}")
        (setq indent-col (- indent-col tab-width)))
      ;; Look backwards for block openers
      (when (and (not (bobp))
                 (save-excursion
                   (forward-line -1)
                   (or (looking-at ".*{\\s-*$")
                       (looking-at ".*:\\s-*$"))))
        (forward-line -1)
        (setq indent-col (+ (current-indentation) tab-width))))
    (indent-line-to (max indent-col 0))
    (when (> (- (point-max) pos) (point))
      (goto-char (- (point-max) pos)))))

;;; ============================================================================
;;; Compilation Mode Integration
;;; ============================================================================

(eval-after-load 'compile
  '(progn
     (add-to-list 'compilation-error-regexp-alist 'aoxim)
     (add-to-list 'compilation-error-regexp-alist-alist
                  '(aoxim
                    "^\\([^:]+\\):\\([0-9]+\\):\\([0-9]+\\): \\(error\\|warning\\): \\(.*\\)$"
                    1 2 3 (4 . nil) 5))))

(defun aoxim-compile ()
  "Compile the current aoxim file."
  (interactive)
  (let* ((file (buffer-file-name))
         (command (concat aoxim-program-name " " 
                         (when aoxim-use-colors "--color ")
                         (shell-quote-argument file))))
    (compile command)))

;;; ============================================================================
;;; Inferior Aoxim Mode (REPL)
;;; ============================================================================

(defvar inferior-aoxim-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "\C-c\C-l" 'aoxim-load-file)
    map)
  "Keymap for inferior aoxim mode.")

(define-derived-mode inferior-aoxim-mode comint-mode "Inferior Aoxim"
  "Major mode for interacting with an inferior aoxim process.

\\{inferior-aoxim-mode-map}"
  (setq comint-prompt-regexp aoxim-prompt-regexp)
  (setq comint-use-prompt-regexp t)
  (setq comint-process-echoes nil)
  (setq comint-prompt-read-only t))

(defvar aoxim-buffer nil
  "The current aoxim process buffer.")

;;;###autoload
(defun run-aoxim ()
  "Run an inferior instance of aoxim."
  (interactive)
  (let* ((aoxim-program (if aoxim-use-colors
                            (concat aoxim-program-name " --color")
                          aoxim-program-name))
         (buffer (comint-check-proc "Aoxim")))
    (pop-to-buffer-same-window
     (if (or buffer (not (derived-mode-p 'inferior-aoxim-mode))
             (comint-check-proc (current-buffer)))
         (get-buffer-create (or buffer "*Aoxim*"))
       (current-buffer)))
    ;; Create the process if there is none
    (unless buffer
      (make-comint-in-buffer "Aoxim" buffer
                            aoxim-program-name nil
                            (when aoxim-use-colors "--color"))
      (inferior-aoxim-mode))
    (setq aoxim-buffer (current-buffer))
    buffer))

(defun aoxim-load-file (file)
  "Load a aoxim FILE into the inferior aoxim process."
  (interactive (list (or (buffer-file-name)
                        (read-file-name "Load aoxim file: " nil nil t))))
  (comint-check-source file)
  (let ((buffer (or aoxim-buffer (run-aoxim))))
    (comint-send-string buffer (format "import \"%s\"\n" file))
    (pop-to-buffer buffer)))

(defun aoxim-send-region (start end)
  "Send the current region to the inferior aoxim process."
  (interactive "r")
  (let ((buffer (or aoxim-buffer (run-aoxim))))
    (comint-send-region buffer start end)
    (comint-send-string buffer "\n")))

(defun aoxim-send-definition ()
  "Send the current definition to the inferior aoxim process."
  (interactive)
  (save-excursion
    (end-of-line)
    (let ((end (point)))
      (beginning-of-line)
      ;; Skip back to start of definition
      (while (and (not (bobp))
                  (save-excursion
                    (forward-line -1)
                    (or (looking-at "^[ \t]*$")
                        (looking-at "^[ \t]+"))))
        (forward-line -1))
      (aoxim-send-region (point) end))))

(defun aoxim-send-buffer ()
  "Send the entire buffer to the inferior aoxim process."
  (interactive)
  (aoxim-send-region (point-min) (point-max)))

(defun aoxim-switch-to-repl ()
  "Switch to the aoxim REPL buffer."
  (interactive)
  (if (and aoxim-buffer (buffer-live-p aoxim-buffer))
      (pop-to-buffer aoxim-buffer)
    (run-aoxim)))

;;; ============================================================================
;;; Keymap
;;; ============================================================================

(defvar aoxim-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "\C-c\C-l" 'aoxim-load-file)
    (define-key map "\C-c\C-r" 'aoxim-send-region)
    (define-key map "\C-c\C-c" 'aoxim-send-definition)
    (define-key map "\C-c\C-b" 'aoxim-send-buffer)
    (define-key map "\C-c\C-z" 'aoxim-switch-to-repl)
    ;; Compilation
    (define-key map "\C-c\C-k" 'aoxim-compile)
    map)
  "Keymap for `aoxim-mode'.")

;;; ============================================================================
;;; Mode Definition
;;; ============================================================================

;;;###autoload
(define-derived-mode aoxim-mode prog-mode "Aoxim"
  "Major mode for editing aoxim math language files.

Key bindings:
\\{aoxim-mode-map}"
  :syntax-table aoxim-mode-syntax-table
  (setq-local font-lock-defaults '(aoxim-font-lock-keywords))
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "#+\\s-*")
  (setq-local indent-line-function #'aoxim-indent-line)
  (setq-local tab-width 4)
  (setq-local electric-indent-chars
              (append '(?\{ ?\} ?\:) electric-indent-chars))
  (setq-local imenu-generic-expression
              '(("Functions" "^\\s-*\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*(" 1)))
  (setq-local which-func-functions
              '(aoxim-which-function)))

(defun aoxim-which-function ()
  "Return the current function name for mode-line display."
  (save-excursion
    (beginning-of-line)
    (when (re-search-backward "^\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*(" nil t)
      (match-string-no-properties 1))))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.aoxim\\'" . aoxim-mode))

;;; ============================================================================
;;; Menu
;;; ============================================================================

(easy-menu-define aoxim-mode-menu aoxim-mode-map
  "Menu for Aoxim mode."
  '("Aoxim"
    ["Start REPL" run-aoxim t]
    ["Switch to REPL" aoxim-switch-to-repl t]
    "---"
    ["Load File" aoxim-load-file t]
    ["Send Region" aoxim-send-region (use-region-p)]
    ["Send Definition" aoxim-send-definition t]
    ["Send Buffer" aoxim-send-buffer t]
    "---"
    ["Compile File" aoxim-compile (buffer-file-name)]
    "---"
    ["Indent Line" indent-for-tab-command t]))

(provide 'aoxim-mode)

;;; aoxim-mode.el ends here
