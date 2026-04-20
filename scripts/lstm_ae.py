"""
LSTM Autoencoder for spoofing detection.

Trains on pure-cancel orders only (cancel_type == 0), which are the candidate
set. The AE learns to reconstruct typical cancellation patterns; outliers
(potential spoofing) surface as high reconstruction error.

Features used (matching Leangurun 2021):
    delta_t, volume_ahead, induced_imbalance, relative_size, price_distance_ticks

Usage:
    python scripts/lstm_ae.py [options]

    python scripts/lstm_ae.py --input features/records.csv
    python scripts/lstm_ae.py --input features/records.csv --epochs 50 --latent 16
"""

import argparse
import sys

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

#  Features

FEATURES = [
    "delta_t",
    "volume_ahead",
    "induced_imbalance",
    "relative_size",
    "price_distance_ticks",
]


def load_and_preprocess(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)

    missing = set(FEATURES) - set(df.columns)
    if missing:
        sys.exit(f"CSV missing columns: {missing}")

    for col in ("delta_t", "volume_ahead", "relative_size"):
        df[col] = np.log1p(df[col])

    for col in FEATURES:
        mu, sigma = df[col].mean(), df[col].std()
        df[col] = (df[col] - mu) / (sigma + 1e-8)

    return df


def build_sequences(values: np.ndarray, window: int) -> np.ndarray:
    """Sliding window: (N - window + 1, window, n_features)"""
    n = len(values)
    if n < window:
        sys.exit(f"Not enough rows ({n}) to build sequences of length {window}.")
    idx = np.arange(window)[None, :] + np.arange(n - window + 1)[:, None]
    return values[idx]


class LSTMAutoencoder(nn.Module):
    """
    Wrapper for the LSTM network provided py PyTorch.
    """

    def __init__(self, n_features: int, latent_dim: int, num_layers: int):
        super().__init__()
        self.encoder = nn.LSTM(
            input_size=n_features,
            hidden_size=latent_dim,
            num_layers=num_layers,
            batch_first=True,
        )
        self.decoder = nn.LSTM(
            input_size=latent_dim,
            hidden_size=latent_dim,
            num_layers=num_layers,
            batch_first=True,
        )
        self.output_layer = nn.Linear(latent_dim, n_features)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        _, (h, _) = self.encoder(x)
        latent = h[-1].unsqueeze(1).repeat(1, x.size(1), 1)
        dec_out, _ = self.decoder(latent)
        return self.output_layer(dec_out)


def train(
    model: nn.Module,
    train_loader: DataLoader,
    val_loader: DataLoader,
    epochs: int,
    lr: float,
    device: torch.device,
) -> tuple[list[float], list[float]]:
    model.to(device)
    optimiser = torch.optim.Adam(model.parameters(), lr=lr)
    criterion = nn.MSELoss()
    train_losses: list[float] = []
    val_losses: list[float] = []

    print(f"  {'epoch':>6}  {'train':>10}  {'val':>10}")
    print(f"  {'-' * 6}  {'-' * 10}  {'-' * 10}")

    for epoch in range(1, epochs + 1):
        model.train()
        epoch_loss = 0.0
        n_train = 0
        for (batch,) in train_loader:
            batch = batch.to(device)
            optimiser.zero_grad()
            recon = model(batch)
            loss = criterion(recon, batch)
            loss.backward()
            optimiser.step()
            epoch_loss += loss.item() * len(batch)
            n_train += len(batch)
        train_avg = epoch_loss / n_train
        train_losses.append(train_avg)

        model.eval()
        val_loss = 0.0
        n_val = 0
        with torch.no_grad():
            for (batch,) in val_loader:
                batch = batch.to(device)
                recon = model(batch)
                val_loss += criterion(recon, batch).item() * len(batch)
                n_val += len(batch)
        val_avg = val_loss / n_val
        val_losses.append(val_avg)

        if epoch % max(1, epochs // 10) == 0:
            print(f"  {epoch:>6d}  {train_avg:>10.6f}  {val_avg:>10.6f}")

    return train_losses, val_losses


@torch.no_grad()
def reconstruction_errors(
    model: nn.Module, sequences: np.ndarray, batch_size: int, device: torch.device
) -> np.ndarray:
    model.eval()
    model.to(device)
    tensor = torch.tensor(sequences, dtype=torch.float32)
    loader = DataLoader(TensorDataset(tensor), batch_size=batch_size)
    errors = []
    for (batch,) in loader:
        batch = batch.to(device)
        recon = model(batch)
        mse = ((recon - batch) ** 2).mean(dim=(1, 2))
        errors.append(mse.cpu().numpy())
    return np.concatenate(errors)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="LSTM-AE spoofing detector")
    p.add_argument("--input", default="features/records.csv")
    p.add_argument("--output", default="features/anomaly_scores.csv")
    p.add_argument(
        "--model-path",
        default="features/lstm_ae.pt",
        help="Where to save/load model weights (default: features/lstm_ae.pt)",
    )
    p.add_argument(
        "--load-model",
        action="store_true",
        help="Skip training and load saved model from --model-path",
    )
    p.add_argument(
        "--window", type=int, default=10, help="Sequence length (default 10)"
    )
    p.add_argument("--latent", type=int, default=16, help="Latent dim (default 16)")
    p.add_argument("--layers", type=int, default=2, help="LSTM layers (default 2)")
    p.add_argument("--epochs", type=int, default=30)
    p.add_argument("--batch-size", type=int, default=256)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument(
        "--threshold-pct",
        type=float,
        default=95.0,
        help="Percentile of train errors used as anomaly threshold",
    )
    p.add_argument("--seed", type=int, default=42)
    return p.parse_args()


def main() -> None:
    args = parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")

    print(f"Loading {args.input} ...")
    df = load_and_preprocess(args.input)
    print(f"  {len(df):,} records loaded")

    pure = df[df["cancel_type"] == 0].reset_index(drop=True)
    print(f"  {len(pure):,} pure-cancel records")

    # Sequences
    values = pure[FEATURES].to_numpy(dtype=np.float32)
    sequences = build_sequences(values, args.window)

    n_train = int(len(sequences) * 0.8)
    train_seqs = sequences[:n_train]
    val_seqs = sequences[n_train:]
    print(
        f"  {len(sequences):,} sequences -> {n_train:,} train / {len(val_seqs):,} val\n"
    )

    train_loader = DataLoader(
        TensorDataset(torch.tensor(train_seqs, dtype=torch.float32)),
        batch_size=args.batch_size,
        shuffle=True,
    )
    val_loader = DataLoader(
        TensorDataset(torch.tensor(val_seqs, dtype=torch.float32)),
        batch_size=args.batch_size,
    )

    # Train or load
    model = LSTMAutoencoder(
        n_features=len(FEATURES),
        latent_dim=args.latent,
        num_layers=args.layers,
    )
    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model: {n_params:,} parameters")

    if args.load_model:
        model.load_state_dict(torch.load(args.model_path, map_location=device))
        print(f"Loaded weights from {args.model_path}\n")
    else:
        print(f"Training for {args.epochs} epochs ...\n")
        train(model, train_loader, val_loader, args.epochs, args.lr, device)
        torch.save(model.state_dict(), args.model_path)
        print(f"\nModel saved to {args.model_path}")

    #  Score all pure-cancel sequences ─
    errors = reconstruction_errors(model, sequences, args.batch_size, device)
    threshold = np.percentile(errors, args.threshold_pct)
    flagged = (errors >= threshold).sum()

    print(
        f"\nReconstruction error  mean={errors.mean():.6f}  "
        f"p50={np.median(errors):.6f}  "
        f"p95={np.percentile(errors, 95):.6f}  "
        f"p99={np.percentile(errors, 99):.6f}"
    )
    print(f"Threshold ({args.threshold_pct}th pct): {threshold:.6f}")
    print(
        f"Flagged as anomalous: {flagged:,} / {len(errors):,} "
        f"({100 * flagged / len(errors):.1f}%)"
    )

    # Write scores
    scores_df = pure.iloc[args.window - 1 :].copy()
    scores_df["recon_error"] = errors
    scores_df["anomaly"] = (errors >= threshold).astype(int)

    scores_df.to_csv(args.output, index=False)
    print(f"\nScores written to {args.output}")


if __name__ == "__main__":
    main()
